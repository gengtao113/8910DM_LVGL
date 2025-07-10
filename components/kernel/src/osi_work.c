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

#include "osi_api.h"
#include "osi_log.h"
#include "osi_internal.h"
#include <string.h>
#include <stdlib.h>
#include <sys/queue.h>

typedef TAILQ_ENTRY(osiWork) osiWorkEntry_t;
typedef TAILQ_HEAD(osiWorkHead, osiWork) osiWorkHead_t;
struct osiWork
{
    osiWorkEntry_t iter;

    osiCallback_t run;
    osiCallback_t complete;
    void *cb_ctx;
    osiWorkQueue_t *wq;
};

struct osiWorkQueue
{
    volatile bool running;
    osiThread_t *thread;
    osiSemaphore_t *work_sema;
    osiSemaphore_t *finish_sema;
    osiWorkHead_t work_list;
};

static osiWorkQueue_t *gHighWq = NULL;
static osiWorkQueue_t *gLowWq = NULL;
static osiWorkQueue_t *gFsWq = NULL;

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

void osiWorkDelete(osiWork_t *work)
{
    if (work == NULL)
        return;

    uint32_t critical = osiEnterCritical();
    if (work->wq != NULL)
    {
        TAILQ_REMOVE(&work->wq->work_list, work, iter);
        work->wq = NULL;
    }
    free(work);
    osiExitCritical(critical);
}

bool osiWorkResetCallback(osiWork_t *work, osiCallback_t run, osiCallback_t complete, void *ctx)
{
    if (work == NULL || run == NULL)
        return false;

    uint32_t critical = osiEnterCritical();
    work->run = run;
    work->complete = complete;
    work->cb_ctx = ctx;
    osiExitCritical(critical);
    return true;
}

bool osiWorkEnqueue(osiWork_t *work, osiWorkQueue_t *wq)
{
    OSI_LOGD(0, "work enqeue, work/%p wq/%p", work, wq);

    if (work == NULL || wq == NULL)
        return false;

    uint32_t critical = osiEnterCritical();
    if (work->wq != wq)
    {
        if (work->wq != NULL)
            TAILQ_REMOVE(&work->wq->work_list, work, iter);

        TAILQ_INSERT_TAIL(&wq->work_list, work, iter);
        work->wq = wq;
        osiSemaphoreRelease(wq->work_sema);
    }
    osiExitCritical(critical);
    return true;
}

bool osiWorkEnqueueLast(osiWork_t *work, osiWorkQueue_t *wq)
{
    OSI_LOGD(0, "work enqeue last, work/%p wq/%p", work, wq);

    if (work == NULL || wq == NULL)
        return false;

    uint32_t critical = osiEnterCritical();

    if (work->wq != NULL)
        TAILQ_REMOVE(&work->wq->work_list, work, iter);

    TAILQ_INSERT_TAIL(&wq->work_list, work, iter);
    work->wq = wq;
    osiSemaphoreRelease(wq->work_sema);

    osiExitCritical(critical);
    return true;
}

void osiWorkCancel(osiWork_t *work)
{
    if (work == NULL)
        return;

    uint32_t critical = osiEnterCritical();
    if (work->wq != NULL)
    {
        TAILQ_REMOVE(&work->wq->work_list, work, iter);
        work->wq = NULL;
    }
    osiExitCritical(critical);
}

bool osiWorkWaitFinish(osiWork_t *work, unsigned timeout)
{
    if (work == NULL)
        return false;

    osiElapsedTimer_t timer;
    osiElapsedTimerStart(&timer);
    uint32_t critical = osiEnterCritical();
    for (;;)
    {
        if (work->wq == NULL)
        {
            osiExitCritical(critical);
            return true;
        }

        if (timeout == 0)
        {
            osiExitCritical(critical);
            return false;
        }

        if (timeout == OSI_WAIT_FOREVER)
        {
            osiSemaphoreAcquire(work->wq->finish_sema);
            continue;
        }

        int wait = timeout - osiElapsedTime(&timer);
        if (wait < 0 || !osiSemaphoreTryAcquire(work->wq->finish_sema, wait))
        {
            osiExitCritical(critical);
            return false;
        }
    }
    // never reach
}

osiCallback_t osiWorkFunction(osiWork_t *work)
{
    return (work != NULL) ? work->run : NULL;
}

void *osiWorkContext(osiWork_t *work)
{
    return (work != NULL) ? work->cb_ctx : NULL;
}

static void _wqThreadEntry(void *argument)
{
    OSI_LOGD(0, "work queue %p started", argument);

    uint32_t critical;
    osiWork_t *work;
    osiWorkQueue_t *wq = (osiWorkQueue_t *)argument;
    wq->thread = osiThreadCurrent();

    OSI_LOGD(0, "work queue thread %p", wq->thread);

    while (wq->running)
    {
        critical = osiEnterCritical();
        work = TAILQ_FIRST(&wq->work_list);
        OSI_LOGD(0, "work run, work/%p wq/%p", work, wq);
        if (work == NULL)
        {
            osiExitCritical(critical);
            osiSemaphoreAcquire(wq->work_sema);
            continue;
        }

        TAILQ_REMOVE(&wq->work_list, work, iter);
        work->wq = NULL;

        // keep the values before exit critical section
        osiCallback_t run = work->run;
        osiCallback_t complete = work->complete;
        void *ctx = work->cb_ctx;
        osiExitCritical(critical);

        if (run != NULL)
            run(ctx);

        if (complete != NULL)
            complete(ctx);

        osiSemaphoreRelease(wq->finish_sema);
    }

    critical = osiEnterCritical();
    while ((work = TAILQ_FIRST(&wq->work_list)) != NULL)
    {
        TAILQ_REMOVE(&wq->work_list, work, iter);
        work->wq = NULL;
    }
    osiExitCritical(critical);

    osiSemaphoreDelete(wq->work_sema);
    osiSemaphoreDelete(wq->finish_sema);
    free(wq);

    osiThreadExit();
}

osiWorkQueue_t *osiWorkQueueCreate(const char *name, size_t thread_count, uint32_t priority, uint32_t stack_size)
{
    osiWorkQueue_t *wq = calloc(1, sizeof(*wq));
    if (wq == NULL)
        return NULL;

    TAILQ_INIT(&wq->work_list);
    wq->running = true;

    wq->work_sema = osiSemaphoreCreate(1, 1);
    wq->finish_sema = osiSemaphoreCreate(1, 0);
    if (wq->work_sema == NULL || wq->finish_sema == NULL)
        goto failed;

    wq->thread = osiThreadCreate(name, _wqThreadEntry, wq, priority, stack_size, 0);
    OSI_LOGD(0, "work queue create thread %p", wq->thread);
    if (wq->thread == NULL)
        goto failed;

    return wq;

failed:
    osiSemaphoreDelete(wq->work_sema);
    osiSemaphoreDelete(wq->finish_sema);
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

osiNotify_t *osiNotifyCreate(osiThread_t *thread, osiCallback_t cb, void *ctx)
{
    if (cb == NULL || thread == NULL)
        return NULL;

    osiNotify_t *notify = malloc(sizeof(osiNotify_t));
    if (notify == NULL)
        return NULL;

    notify->thread = thread;
    notify->cb = cb;
    notify->ctx = ctx;
    notify->status = OSI_NOTIFY_IDLE;
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

void osiNotifyTrigger(osiNotify_t *notify)
{
    uint32_t critical = osiEnterCritical();
    if (notify->status == OSI_NOTIFY_IDLE)
    {
        osiEvent_t event = {
            .id = OSI_EVENT_ID_NOTIFY,
            .param1 = (uint32_t)notify,
        };

        notify->status = OSI_NOTIFY_QUEUED_ACTIVE;
        osiEventSend(notify->thread, &event);
    }
    else if (notify->status != OSI_NOTIFY_QUEUED_DELETE)
    {
        notify->status = OSI_NOTIFY_QUEUED_ACTIVE;
    }
    osiExitCritical(critical);
}

void osiNotifyCancel(osiNotify_t *notify)
{
    uint32_t critical = osiEnterCritical();
    if (notify->status == OSI_NOTIFY_QUEUED_ACTIVE)
        notify->status = OSI_NOTIFY_QUEUED_CANCEL;
    osiExitCritical(critical);
}
