/* Copyright (C) 2018 RDA Technologies Limited and/or its affiliates("RDA").
 * All rights reserved.
 *
 * This software is supplied "AS IS" without any warranties.
 * RDA assumes no responsibility or liability for the use of the software,
 * conveys no license or title under any patent, copyright, or mask work
 * right to the product. RDA reserves the right to make changes in the
 * software without notification.  RDA also make no representation or
 * warranty that such application will be suitable for the specified use
 * without further testing or modification.
 */

// #define OSI_LOCAL_LOG_LEVEL OSI_LOG_LEVEL_DEBUG

/**
 * @file osi_work_queue.h
 * @brief 轻量级工作队列机制（Work Queue），用于延迟执行任务或解耦任务执行上下文
 *
 * 本模块定义了 `osiWorkQueue_t`（工作队列）和 `osiWork_t`（工作项）结构，用于支持多线程环境中
 * 异步任务调度。通过将任务（work）投递到后台线程（thread）中执行，避免在中断或主线程中执行耗时逻辑。
 *
 * 支持任务执行前回调（`run`）和完成后回调（`complete`），并可分高/低/文件系统优先级队列。
 */

#include "osi_api.h"
#include "osi_log.h"
#include "osi_internal.h"
#include <string.h>
#include <stdlib.h>
#include <sys/queue.h>

typedef TAILQ_ENTRY(osiWork) osiWorkEntry_t;
typedef TAILQ_HEAD(osiWorkHead, osiWork) osiWorkHead_t;

/**
 * 整理后：
 * /**
 * @brief osiWork 链表节点的链接信息
 * 用于将 osiWork 对象挂接到双向队列中。
 *
 *   typedef struct {
 *       struct osiWork *tqe_next;         ///< 指向下一个节点
 *       struct osiWork **tqe_prev;        ///< 指向前一个节点的 tqe_next 成员的指针
 *   } osiWorkEntry_t;
 * 
 * @brief osiWork 链表头
 * 包含指向第一个元素的指针和指向最后一个元素 next 指针地址的指针。
 *
 *  typedef struct {
 *      struct osiWork *tqh_first;        ///< 指向链表第一个元素
 *      struct osiWork **tqh_last;        ///< 指向最后一个元素的 tqe_next 指针的地址
 *  } osiWorkHead_t;
 */


struct osiWork
{
    osiWorkEntry_t iter;       ///< 链表节点，用于挂载在工作队列上

    osiCallback_t run;         ///< 工作主函数指针，在工作线程中被调用
    osiCallback_t complete;    ///< 工作完成回调函数，可选，通常在主线程中调用

    void *cb_ctx;              ///< 回调函数的上下文参数

    osiWorkQueue_t *wq;        ///< 所属的工作队列（用于完成通知、状态检查等）
};


struct osiWorkQueue
{
    volatile bool running;         ///< 标志工作队列是否仍在运行（用于线程退出判断）

    osiThread_t *thread;           ///< 工作队列使用的后台线程对象（自动处理工作项）

    osiSemaphore_t *work_sema;     ///< 工作同步信号量（有新工作到来时释放，用于唤醒线程）
    osiSemaphore_t *finish_sema;   ///< 完成同步信号量（工作执行完成时用于通知等待者）

    osiWorkHead_t work_list;       ///< 挂载所有工作项的链表队列
};


static osiWorkQueue_t *gHighWq = NULL;  ///< 高优先级工作队列（例如 UI、音频等）
static osiWorkQueue_t *gLowWq = NULL;   ///< 低优先级工作队列（例如日志、异步写入等）
static osiWorkQueue_t *gFsWq = NULL;    ///< 文件系统相关工作队列

/**************************************************************
 * @brief 创建一个工作项对象，用于异步任务调度系统（如工作队列）。
 *
 * @param run      工作项主函数（必须提供），实际执行的任务。
 * @param complete 工作项完成后的回调函数（可选），用于通知或清理。
 * @param ctx      用户上下文指针，会传递给 run 和 complete，用于传参。
 *
 * @return 成功返回指向新创建的工作项指针（osiWork_t *），失败返回 NULL。
 *
 * @note 创建后通常会将该工作项加入某个工作队列（wq）中调度执行。
 ****************************************************************/
osiWork_t *osiWorkCreate(osiCallback_t run, osiCallback_t complete, void *ctx)
{
    // 如果 run 为 NULL，说明调用者没有提供任务处理函数
    // 工作项无法正常执行，因此直接返回 NULL
    if (run == NULL)
        return NULL;
    // 使用 calloc 分配并初始化一个 osiWork_t 结构体
    // calloc(1, sizeof(*work)) 分配了一个结构体大小的内存，并全部置 0
    osiWork_t *work = calloc(1, sizeof(*work));
    // 如果内存分配失败（系统资源不足），返回 NULL
    if (work == NULL)
        return NULL;
    // 将工作项的 run 函数指针设置为用户传入的处理函数
    work->run = run;
    // 设置 complete 回调（可以为 NULL，不强制）
    // 如果提供了，它会在任务完成后被调用
    work->complete = complete;
    // 保存用户上下文指针，这个值会传入 run 和 complete，用于传递参数
    work->cb_ctx = ctx;
    // 初始化工作项所属的工作队列指针为 NULL
    // 后续通过调度系统将该工作项插入某个队列时会赋值
    work->wq = NULL;
    // 所有字段初始化完成，返回工作项对象指针
    return work;
}

/**
 * @brief 删除一个工作任务（osiWork）
 *
 * 该函数从工作队列中安全地移除一个任务对象并释放其内存资源。
 * 如果该任务尚在队列中，会先从队列中移除，并设置其工作队列指针为空。
 *
 * @param work 要删除的工作任务指针。传入 NULL 则无操作。
 */
void osiWorkDelete(osiWork_t *work)
{
    // 如果参数为空，直接返回
    if (work == NULL)
        return;

    // 进入临界区，防止多线程并发访问
    uint32_t critical = osiEnterCritical();

    // 如果当前任务已经加入了某个工作队列
    if (work->wq != NULL)
    {
        // 从工作队列中移除该任务
        TAILQ_REMOVE(&work->wq->work_list, work, iter);

        // 断开与工作队列的关联，表示未挂载
        work->wq = NULL;
    }

    // 释放该任务对象占用的内存
    free(work);

    // 退出临界区
    osiExitCritical(critical);
}


/**
 * @brief 重设工作任务的回调函数及上下文
 *
 * 此函数允许在不重新创建对象的情况下，更新任务的主执行函数、
 * 完成回调函数及回调上下文数据。
 *
 * @param work     要更新的工作任务指针
 * @param run      新的任务执行函数指针（必填）
 * @param complete 新的任务完成回调函数（可为空）
 * @param ctx      用户上下文指针（可为空）
 * @return true    成功更新回调信息
 * @return false   参数非法或 work/run 为 NULL
 */
bool osiWorkResetCallback(osiWork_t *work, osiCallback_t run, osiCallback_t complete, void *ctx)
{
    // 参数校验：任务和运行回调函数必须非空
    if (work == NULL || run == NULL)
        return false;

    // 进入临界区，防止竞争条件
    uint32_t critical = osiEnterCritical();

    // 更新运行函数、完成回调及上下文
    work->run = run;
    work->complete = complete;
    work->cb_ctx = ctx;

    // 退出临界区
    osiExitCritical(critical);

    return true;
}


/**
 * @brief 将工作项加入工作队列中，等待后续调度执行。
 *
 * @param work 指向待加入的工作项（osiWork_t）。
 * @param wq   指向目标工作队列（osiWorkQueue_t）。
 *
 * @return 加入成功返回 true，参数非法或失败返回 false。
 *
 * @note 若该工作项已在其他工作队列中，会先从原队列中移除，再加入目标队列。
 *       加入后会通过信号量通知队列有新任务到来。
 */
bool osiWorkEnqueue(osiWork_t *work, osiWorkQueue_t *wq)
{
    // 打印调试日志：打印工作项和目标工作队列的地址
    OSI_LOGD(0, "work enqeue, work/%p wq/%p", work, wq);
    // 如果工作项或工作队列为空指针，参数非法，直接返回 false
    if (work == NULL || wq == NULL)
        return false;
    // 进入临界区，防止多线程/中断同时访问队列（加锁）
    uint32_t critical = osiEnterCritical();
    // 如果工作项当前绑定的队列不是目标队列
    if (work->wq != wq)
    {
        // 如果已经在某个队列中，先从旧队列中移除该工作项
        if (work->wq != NULL)
            TAILQ_REMOVE(&work->wq->work_list, work, iter);
        // 将工作项插入到新队列的末尾（链表尾部）
        TAILQ_INSERT_TAIL(&wq->work_list, work, iter);
        // 更新工作项的队列指针，指向新队列
        work->wq = wq;
        // 释放信号量，通知工作线程：有新任务到达
        osiSemaphoreRelease(wq->work_sema);
    }
    // 离开临界区，恢复中断或多线程调度
    osiExitCritical(critical);
    return true;
}

/**
 * @brief 将工作任务添加到工作队列末尾（尾插）
 *
 * 如果该任务已在其他工作队列中，会先将其从原队列中移除，再插入新的队列。
 * 同时通过信号量唤醒等待执行任务的线程。
 *
 * @param work 工作任务对象指针（必须已初始化）
 * @param wq   目标工作队列指针
 * @return true 成功插入
 * @return false 参数为空或无效
 */
bool osiWorkEnqueueLast(osiWork_t *work, osiWorkQueue_t *wq)
{
    OSI_LOGD(0, "work enqeue last, work/%p wq/%p", work, wq);

    // 参数检查
    if (work == NULL || wq == NULL)
        return false;

    // 进入临界区，保证多线程安全
    uint32_t critical = osiEnterCritical();

    // 若任务已存在于其他队列中，先移除
    if (work->wq != NULL)
        TAILQ_REMOVE(&work->wq->work_list, work, iter);

    // 将任务插入到目标队列尾部
    TAILQ_INSERT_TAIL(&wq->work_list, work, iter);

    // 设置任务当前所属工作队列
    work->wq = wq;

    // 通知工作队列线程有新任务
    osiSemaphoreRelease(wq->work_sema);

    // 退出临界区
    osiExitCritical(critical);
    return true;
}


/**
 * @brief 取消工作任务（若已挂载于工作队列中）
 *
 * 该函数会从任务所在的工作队列中将其移除，并断开任务与队列的关联。
 *
 * @param work 要取消的任务对象指针
 */
void osiWorkCancel(osiWork_t *work)
{
    // 空指针检测
    if (work == NULL)
        return;

    // 进入临界区，保护工作队列一致性
    uint32_t critical = osiEnterCritical();

    // 如果任务挂在某个工作队列中
    if (work->wq != NULL)
    {
        // 从队列中安全移除任务
        TAILQ_REMOVE(&work->wq->work_list, work, iter);

        // 清空队列引用，标记为未挂载
        work->wq = NULL;
    }

    // 退出临界区
    osiExitCritical(critical);
}

/**
 * @brief 等待一个工作任务完成
 *
 * 此函数用于阻塞当前线程，直到指定的工作项执行完成，或等待超时。
 * 工作完成的依据是该工作从其所属的工作队列中被移除（`work->wq == NULL`）。
 *
 * @param work    待等待的工作项指针
 * @param timeout 等待超时时间（毫秒），可以是 OSI_WAIT_FOREVER 表示永久等待
 * @return true  表示工作已经完成
 *         false 表示超时或参数非法
 *
 * @note 当设置为永久等待时，若任务永不完成，会导致阻塞。
 *       本函数线程安全，可被多个线程调用。
 */

bool osiWorkWaitFinish(osiWork_t *work, unsigned timeout)
{
    // 参数非法，直接返回 false
    if (work == NULL)
        return false;

    // 定义并启动一个计时器，用于计算超时时间
    osiElapsedTimer_t timer;
    osiElapsedTimerStart(&timer);

    // 进入临界区，防止并发修改 work->wq
    uint32_t critical = osiEnterCritical();

    // 无限循环，直到任务完成或超时
    for (;;)
    {
        // 如果任务已经完成（即已从工作队列中移除）
        if (work->wq == NULL)
        {
            osiExitCritical(critical);
            return true; // 成功等待完成
        }

        // timeout=0，表示立即返回，不等待
        if (timeout == 0)
        {
            osiExitCritical(critical);
            return false; // 任务未完成
        }

        // 永久等待模式
        if (timeout == OSI_WAIT_FOREVER)
        {
            // 阻塞式等待“完成信号量”，等待任何一个任务完成
            osiSemaphoreAcquire(work->wq->finish_sema);
            // 继续检查 work->wq 是否已清除（有可能不是当前任务完成）
            continue;
        }

        // 否则进入有限时间等待，计算剩余时间
        int wait = timeout - osiElapsedTime(&timer);
        if (wait < 0 || !osiSemaphoreTryAcquire(work->wq->finish_sema, wait))
        {
            osiExitCritical(critical);
            return false; // 超时或等待失败
        }

        // 若拿到信号量，继续循环检查任务是否完成
    }

    // 理论上不可达：循环要么返回 true，要么返回 false
    // return false;
}

osiCallback_t osiWorkFunction(osiWork_t *work)
{
    return (work != NULL) ? work->run : NULL;
}

void *osiWorkContext(osiWork_t *work)
{
    return (work != NULL) ? work->cb_ctx : NULL;
}
/**
 * @brief 工作队列线程的主入口函数（work queue worker thread）。
 *
 * 该函数由 `osiWorkQueueCreate()` 创建的线程执行，
 * 用于处理挂到工作队列上的所有任务（`osiWork_t`）。
 *
 * 工作流程：
 * - 线程初始化后进入循环
 * - 每次从队列中取出一个任务执行
 * - 调用任务的 run 回调、然后是 complete 回调
 * - 使用信号量机制控制线程阻塞与唤醒
 * - 线程退出前清理队列中未执行的任务并释放资源
 */

static void _wqThreadEntry(void *argument)
{
     // 打印日志：工作队列线程启动
    OSI_LOGD(0, "work queue %p started", argument);

    uint32_t critical;
    osiWork_t *work;
    // 将传入的参数转换为工作队列结构体指针
    osiWorkQueue_t *wq = (osiWorkQueue_t *)argument;
    // 记录当前线程指针（通常在线程内部调用 osiThreadCurrent）
    wq->thread = osiThreadCurrent();

    OSI_LOGD(0, "work queue thread %p", wq->thread);
    // 主循环，只要工作队列处于“运行”状态就不断检查任务列表
    /*wq->running 变为 false 是由其他线程主动调用某个释放函数（比如 osiWorkQueueDelete()）设置的
    *为什么要这样设计？
    *[1]防止线程死循环：运行标志 wq->running 是个“线程退出开关”。
    *[2]防止资源泄漏：在线程退出前清理队列中还没执行的任务。
    *[3]实现可控线程生命周期：比如动态创建和销毁模块线程时很实用。
    */
   while (wq->running)
    {
        // 进入临界区，保护 work_list 链表访问
        critical = osiEnterCritical();
        // 取出工作队列中的第一个待处理任务
        work = TAILQ_FIRST(&wq->work_list);
        OSI_LOGD(0, "work run, work/%p wq/%p", work, wq);
        // 如果当前没有任务可执行
        if (work == NULL)
        {
            // 退出临界区
            osiExitCritical(critical);
            // 等待新任务到来（被唤醒）
            osiSemaphoreAcquire(wq->work_sema);
            continue;// 回到循环头部
        }
        // 将任务从队列中移除
        TAILQ_REMOVE(&wq->work_list, work, iter);
        // 清空该任务的工作队列指针（表示不再挂靠在队列上）
        work->wq = NULL;

        // keep the values before exit critical section
        // 在临界区中读取任务数据后退出临界区（防止阻塞其它操作）
        osiCallback_t run = work->run;
        osiCallback_t complete = work->complete;
        void *ctx = work->cb_ctx;
        osiExitCritical(critical);
        // 如果任务定义了 run 回调函数，调用它（执行任务主逻辑）
        if (run != NULL)
            run(ctx);
        // 如果任务定义了 complete 回调函数，调用它（处理收尾逻辑）
        if (complete != NULL)
            complete(ctx);
        // 通知任务完成，可以释放资源或等待 finish 信号
        osiSemaphoreRelease(wq->finish_sema);
    }
     // 线程即将退出，清理队列中尚未执行的任务（防止泄露）
    critical = osiEnterCritical();
    while ((work = TAILQ_FIRST(&wq->work_list)) != NULL)
    {
        TAILQ_REMOVE(&wq->work_list, work, iter);
        work->wq = NULL;
    }
    osiExitCritical(critical);
    // 删除工作队列的两个信号量（防止内存泄漏）
    osiSemaphoreDelete(wq->work_sema);
    osiSemaphoreDelete(wq->finish_sema);
    // 释放工作队列结构体本身
    free(wq);
    // 正常退出线程
    osiThreadExit();
}
/**
 * @brief 创建一个工作队列对象（带工作线程和调度机制）
 *
 * 工作队列用于在独立线程中顺序执行异步任务（如回调函数、耗时操作等），
 * 它包含一个工作项链表、一个调度线程以及两个用于同步的信号量。
 *
 * @param name         工作线程名（便于调试）
 * @param thread_count 保留参数（当前仅支持 1）
 * @param priority     工作线程的优先级
 * @param stack_size   工作线程的栈大小（字节）
 * @return 成功时返回工作队列对象指针，失败时返回 NULL
 *
 * @note 工作队列的线程会运行 `_wqThreadEntry()` 函数，不断处理已提交的任务。
 */

osiWorkQueue_t *osiWorkQueueCreate(const char *name, size_t thread_count, uint32_t priority, uint32_t stack_size)
{
    // 分配工作队列对象的内存，并清零
    osiWorkQueue_t *wq = calloc(1, sizeof(*wq));
    if (wq == NULL)
        return NULL;

    // 初始化工作队列链表（任务列表）
    TAILQ_INIT(&wq->work_list);

    // 标记该工作队列处于运行状态
    wq->running = true;

    // 创建用于工作线程唤醒的信号量，初始值为 1（允许进入）
    wq->work_sema = osiSemaphoreCreate(1, 1);
    // 创建用于等待所有任务完成的信号量，初始为 0（阻塞）
    wq->finish_sema = osiSemaphoreCreate(1, 0);
    // 创建信号量失败，跳转至清理逻辑
    if (wq->work_sema == NULL || wq->finish_sema == NULL)
        goto failed;

    // 创建专用的工作线程，线程入口函数为 _wqThreadEntry，参数为当前工作队列对象
    wq->thread = osiThreadCreate(name, _wqThreadEntry, wq, priority, stack_size, 0);
    OSI_LOGD(0, "work queue create thread %p", wq->thread);
    // 创建线程失败，跳转至清理逻辑
    if (wq->thread == NULL)
        goto failed;

    // 所有资源初始化成功，返回工作队列指针
    return wq;

failed:
    // 清理已分配的信号量资源
    osiSemaphoreDelete(wq->work_sema);
    osiSemaphoreDelete(wq->finish_sema);
    // 释放工作队列结构体内存
    free(wq);
    return NULL;
}

void osiWorkQueueDelete(osiWorkQueue_t *wq)
{
    if (wq == NULL)
        return;

    // Potentially, it is possible work_sema will be deleted after running
    // is set to false.
    unsigned critical = osiEnterCritical();
    wq->running = false;
    osiSemaphoreRelease(wq->work_sema);
    osiExitCritical(critical);
}

osiWorkQueue_t *osiSysWorkQueueHighPriority(void) { return gHighWq; }
osiWorkQueue_t *osiSysWorkQueueLowPriority(void) { return gLowWq; }
osiWorkQueue_t *osiSysWorkQueueFileWrite(void) { return gFsWq; }

void osiSysWorkQueueInit(void)
{
    if (gHighWq == NULL)
        gHighWq = osiWorkQueueCreate(
            "wq_hi", 1, OSI_PRIORITY_HIGH, CONFIG_KERNEL_HIGH_PRIO_WQ_STACKSIZE);
    if (gLowWq == NULL)
        gLowWq = osiWorkQueueCreate(
            "wq_lo", 1, OSI_PRIORITY_LOW, CONFIG_KERNEL_LOW_PRIO_WQ_STACKSIZE);
    if (gFsWq == NULL)
        gFsWq = osiWorkQueueCreate(
            "wq_fs", 1, OSI_PRIORITY_BELOW_NORMAL, CONFIG_KERNEL_FILE_WRITE_WQ_STACKSIZE);
}

/*
 * Function Name  : osiNotifyCreate
 * Description    : 创建一个通知对象（osiNotify_t 实例），用于在指定线程中触发回调函数。
 *                  可用于线程间异步事件通知，配合 osiNotifyTrigger 使用。
 * Input          : 
 *      thread : 指向目标线程的指针（回调将在此线程上下文中执行）
 *      cb     : 通知回调函数指针，不能为空
 *      ctx    : 用户自定义的上下文指针，将在回调时传入
 * Output         : 无
 * Return         : 
 *      成功时返回分配并初始化好的通知对象指针；
 *      失败（如分配内存失败或参数非法）返回 NULL。
 */

osiNotify_t *osiNotifyCreate(osiThread_t *thread, osiCallback_t cb, void *ctx)
{
    // 基本参数检查：回调函数和目标线程不能为空
    if (cb == NULL || thread == NULL)
        return NULL;
    // 为通知对象结构体分配内存（此对象生命周期需调用者自行管理）
    osiNotify_t *notify = malloc(sizeof(osiNotify_t));
    // 内存分配失败，返回 NULL
    if (notify == NULL)
        return NULL;
    // 设置通知目标线程（回调将通过此线程的消息机制触发）
    notify->thread = thread;
    // 设置通知的回调函数指针
    notify->cb = cb;
    // 保存用户上下文指针（用于回调时传入）
    notify->ctx = ctx;
    // 初始化通知状态为“空闲”
    notify->status = OSI_NOTIFY_IDLE;
    // 返回创建好的通知对象
    return notify;
}

void osiNotifyDelete(osiNotify_t *notify)
{
    if (notify == NULL)
        return;

    uint32_t critical = osiEnterCritical();
    if (notify->status == OSI_NOTIFY_IDLE)
        free(notify);
    else
        notify->status = OSI_NOTIFY_QUEUED_DELETE;
    osiExitCritical(critical);
}

/*
 * Function Name  : osiNotifyTrigger
 * Description    : 触发指定的通知对象，在其关联线程中异步执行回调函数。
 *                  通知机制基于事件队列，线程收到事件后将调用 notify->cb。
 *
 * Input          : 
 *      notify : 要触发的通知对象指针，由 osiNotifyCreate 创建。
 * Output         : None
 * Return         : None
 *
 * Notes:
 *   - 此函数是线程安全的，可从任意线程触发。
 *   - 回调将在目标线程的上下文中执行，确保不会跨线程执行。
 *   - 若多次触发，只会进入队列一次（除非删除状态）。
 */

void osiNotifyTrigger(osiNotify_t *notify)
{
    // 进入临界区，防止并发访问导致状态混乱
    uint32_t critical = osiEnterCritical();
    // 如果通知当前处于空闲状态，说明尚未入队
    if (notify->status == OSI_NOTIFY_IDLE)
    {
        // 构造一个事件对象，用于投递给目标线程
        osiEvent_t event = {
            .id = OSI_EVENT_ID_NOTIFY, // 通知事件 ID
            .param1 = (uint32_t)notify,  // 将通知对象指针作为参数传入
        };
        // 设置状态为“已入队激活中”
        notify->status = OSI_NOTIFY_QUEUED_ACTIVE;
        // 向通知目标线程投递事件，线程将从队列中接收并调用 notify->cb
        osiEventSend(notify->thread, &event);
    }
    // 如果不是已标记为“待删除”状态（QUEUED_DELETE），则仍可更新状态
    else if (notify->status != OSI_NOTIFY_QUEUED_DELETE)
    {
        // 保持/更新状态为“激活中”，防止被错误地标记为空闲
        notify->status = OSI_NOTIFY_QUEUED_ACTIVE;
    }
    // 离开临界区，恢复中断
    osiExitCritical(critical);
}

void osiNotifyCancel(osiNotify_t *notify)
{
    uint32_t critical = osiEnterCritical();
    if (notify->status == OSI_NOTIFY_QUEUED_ACTIVE)
        notify->status = OSI_NOTIFY_QUEUED_CANCEL;
    osiExitCritical(critical);
}
