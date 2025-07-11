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

#include "osi_pipe.h"
#include "osi_api.h"
#include <stdlib.h>
#include <string.h>

#include "quec_proj_config.h"

/**
 * @brief      OSI 管道缓冲结构体（osiPipe）
 *
 * @details
 * 该结构体实现了一个基于环形缓冲区的线程间数据管道（pipe）。
 * 它支持多线程之间的异步数据读写，同时提供信号量同步与事件回调机制。
 * 常用于系统组件、任务或驱动模块之间进行数据交换。
 *
 * 管道的核心功能包括：
 *  - 环形缓冲管理（读指针/写指针）
 *  - 数据可读/可写通知（信号量）
 *  - 支持数据事件回调（可配置掩码）
 *  - 支持 EOF 结束标志
 *  - 可用于阻塞与非阻塞通信
 */
struct osiPipe
{
    /**< 标记管道是否处于运行中。用于控制是否继续读写操作。 */
    volatile bool running;
      /**< EOF（End of File）标志，标识写端已关闭，读端可据此判断是否终止读取。 */
    volatile bool eof;
    /**< 环形缓冲区大小，单位为字节（由分配内存决定）。 */
    unsigned size;
      /**< 当前读指针，表示下次读取数据的位置（偏移量）。 */
    unsigned rd;
     /**< 当前写指针，表示下次写入数据的位置（偏移量）。 */
    unsigned wr;
     /**< 用于控制“可读”状态的信号量。当没有数据可读时，读者阻塞等待此信号量。 */
    osiSemaphore_t *rd_avail_sema;
     /**< 用于控制“可写”状态的信号量。当缓冲满时，写者阻塞等待此信号量。 */
    osiSemaphore_t *wr_avail_sema;
    /**< 读回调触发事件掩码，用于指定在哪些条件下触发 rd_cb。 */
    unsigned rd_cb_mask;
    /**< 读事件回调函数指针（如数据可读时触发）。 */
    osiPipeEventCallback_t rd_cb;
    /**< 读事件回调的上下文指针，作为回调函数的参数。 */
    void *rd_cb_ctx;
    /**< 写回调触发事件掩码，用于指定在哪些条件下触发 wr_cb。 */
    unsigned wr_cb_mask;
     /**< 写事件回调函数指针（如缓冲可写时触发）。 */
    osiPipeEventCallback_t wr_cb;
     /**< 写事件回调的上下文指针，作为回调函数的参数。 */
    void *wr_cb_ctx;
    /**< 标记数据是否已经写完/处理完（具体用途视实现而定）。 */
	char data_done;
    /**< 数据缓冲区本体，采用柔性数组形式，初始化时动态分配大小。 */
    char data[];
};

#ifdef CONFIG_QUEC_PROJECT_FEATURE_AUDIO
void osiPipeDataEnd(osiPipe_t *pipe)
{
	pipe->data_done = 1;
}
#endif

/**
 * @brief       创建一个 OSI 管道对象（osiPipe）
 * 
 * @details
 * 分配并初始化一个用于线程间通信的环形缓冲区结构体 `osiPipe_t`。
 * 管道支持异步读写，并提供信号量同步机制。
 * 
 * - 管道缓冲区通过柔性数组 `data[]` 动态分配；
 * - 读写端各自拥有一个信号量用于控制阻塞与唤醒；
 * - 若资源创建失败，自动进行清理回退。
 * 
 * @param size  管道数据缓冲区的字节大小，必须大于 0
 * @return      成功返回指向 osiPipe_t 的指针，失败返回 NULL
 */
osiPipe_t *osiPipeCreate(unsigned size)
{
    // 若传入的 size 为 0，非法参数，无法创建管道
    if (size == 0)
        return NULL;

    // 分配 osiPipe_t 结构体加上实际数据缓冲区（柔性数组）
    // calloc 确保结构体字段初始化为 0
    osiPipe_t *pipe = (osiPipe_t *)calloc(1, sizeof(osiPipe_t) + size);
    // 内存分配失败，直接返回
    if (pipe == NULL)
        return NULL;

    // 初始化成员变量
    pipe->running = true;  // 设置管道为运行状态
    pipe->eof = false;     // 初始不设置 EOF（写端未关闭）
    pipe->size = size;     // 记录缓冲区大小

    // 创建用于写入可用的信号量（初始值为 1，表示缓冲区可写）
    pipe->wr_avail_sema = osiSemaphoreCreate(1, 1);
    // 创建用于读取可用的信号量（初始值为 0，表示暂不可读）
    pipe->rd_avail_sema = osiSemaphoreCreate(1, 0);
     // 如果任一信号量创建失败，进行清理，防止资源泄露
    if (pipe->wr_avail_sema == NULL || pipe->rd_avail_sema == NULL)
    {
        // 安全删除已分配的信号量（即使其中一个是 NULL 也无副作用）
        osiSemaphoreDelete(pipe->wr_avail_sema);
        osiSemaphoreDelete(pipe->rd_avail_sema);
         // 释放结构体内存
        free(pipe);
        return NULL;
    }
    // 成功返回 pipe 指针
    return pipe;
}

void osiPipeDelete(osiPipe_t *pipe)
{
    if (pipe == NULL)
        return;

    osiSemaphoreDelete(pipe->wr_avail_sema);
    osiSemaphoreDelete(pipe->rd_avail_sema);
    free(pipe);
}

void osiPipeReset(osiPipe_t *pipe)
{
    if (pipe == NULL)
        return;

    uint32_t critical = osiEnterCritical();
    pipe->rd = 0;
    pipe->wr = 0;
    pipe->running = true;
    pipe->eof = false;
    osiExitCritical(critical);
}

void osiPipeStop(osiPipe_t *pipe)
{
    uint32_t critical = osiEnterCritical();
    pipe->running = false;
    osiSemaphoreRelease(pipe->wr_avail_sema);
    osiSemaphoreRelease(pipe->rd_avail_sema);
    osiExitCritical(critical);
}

bool osiPipeIsStopped(osiPipe_t *pipe)
{
    return !pipe->running;
}

void osiPipeSetEof(osiPipe_t *pipe)
{
    uint32_t critical = osiEnterCritical();
    pipe->eof = true;
    osiSemaphoreRelease(pipe->wr_avail_sema);
    osiSemaphoreRelease(pipe->rd_avail_sema);
    osiExitCritical(critical);
}

bool osiPipeIsEof(osiPipe_t *pipe)
{
    return pipe->eof;
}

void osiPipeSetWriterCallback(osiPipe_t *pipe, unsigned mask, osiPipeEventCallback_t cb, void *ctx)
{
    if (pipe == NULL)
        return;

    pipe->wr_cb_mask = mask;
    pipe->wr_cb = cb;
    pipe->wr_cb_ctx = ctx;
}

void osiPipeSetReaderCallback(osiPipe_t *pipe, unsigned mask, osiPipeEventCallback_t cb, void *ctx)
{
    if (pipe == NULL)
        return;

    pipe->rd_cb_mask = mask;
    pipe->rd_cb = cb;
    pipe->rd_cb_ctx = ctx;
}
/*
 * Function Name  : osiPipeRead
 * Description    : 从 osiPipe 管道中读取指定大小的数据，支持环形缓冲区管理、并发同步、EOF 检测、读回调通知等功能。
 * Input          : 
 *      pipe : 管道指针，必须由 osiPipeCreate() 创建
 *      buf  : 目标缓冲区指针，用于存放读取的数据
 *      size : 要读取的最大字节数
 * Output         : 
 *      将数据写入到 buf 中
 * Return         : 
 *      成功读取的字节数，失败返回 -1，若无数据返回 0
 */

int osiPipeRead(osiPipe_t *pipe, void *buf, unsigned size)
{
     // 边界条件：读取 0 字节视为无效操作
    if (size == 0)
        return 0;
    // 参数合法性校验
    if (pipe == NULL || buf == NULL)
        return -1;
    // 进入临界区，防止读写指针在多线程中被并发修改
    uint32_t critical = osiEnterCritical();
    // 计算当前可读的字节数
    unsigned bytes = pipe->wr - pipe->rd;
    // 实际读取长度 = min(请求大小, 可用数据)
    unsigned len = OSI_MIN(unsigned, size, bytes);
    // 保存当前读指针位置
    unsigned rd = pipe->rd;

    // 若管道已关闭（running==false），直接返回错误
    if (!pipe->running)
    {
        osiExitCritical(critical);
        return -1;
    }

#ifdef CONFIG_QUEC_PROJECT_FEATURE_AUDIO
    // 特殊逻辑：若已标记 data_done 且没有数据，触发 EOF 并返回
	if(pipe->data_done == 1 && bytes == 0)
	{
		osiPipeSetEof(pipe); // 设置 EOF 标志
		return -1;
	}
#endif
    // 若无数据可读，直接返回 0
    if (len == 0)
    {
        osiExitCritical(critical);
        return 0;
    }
    // 环形 buffer 拷贝计算：当前读偏移
    unsigned offset = rd % pipe->size;
    // 计算缓冲区末尾剩余空间（从 offset 到尾部）
    unsigned tail = pipe->size - offset;
    // 若拷贝数据不跨尾部，直接拷贝
    if (tail >= len)
    {
        memcpy(buf, &pipe->data[offset], len);
    }
    else
    {
        // 若跨越环形尾部，分两段拷贝
        memcpy(buf, &pipe->data[offset], tail);
        memcpy((char *)buf + tail, pipe->data, len - tail);
    }
    // 更新读指针
    pipe->rd += len;
     // 退出临界区，允许其他线程访问管道
    osiExitCritical(critical);
    // 如果这次读完了所有写入的数据（即读光了），触发写完成回调
    if (len == bytes)
    {
        if (pipe->wr_cb != NULL && (pipe->wr_cb_mask & OSI_PIPE_EVENT_TX_COMPLETE))
            pipe->wr_cb(pipe->wr_cb_ctx, OSI_PIPE_EVENT_TX_COMPLETE);
    }
    // 通知写端有空余空间可写（对应 wr_avail_sema）
    osiSemaphoreRelease(pipe->wr_avail_sema);
    // 返回成功读取的字节数
    return len;
}

/**
 * @brief       向 OSI 管道写入数据（非阻塞写）
 *
 * @details
 * 向指定的 `osiPipe_t` 管道结构写入数据。
 * 管道是一个线程安全的环形缓冲区，支持并发写入。
 * 
 * - 写入时会自动处理缓冲区环绕；
 * - 写入完成后会触发读端回调（若注册）；
 * - 若缓冲区空间不足，仅写入部分数据；
 * - 若管道已关闭或 EOF 标记置位，则写入失败。
 * 
 * @param pipe  管道对象指针
 * @param buf   要写入的数据缓冲区
 * @param size  要写入的数据长度（字节）
 * @return      实际写入的字节数，写入失败返回 -1
 */
int osiPipeWrite(osiPipe_t *pipe, const void *buf, unsigned size)
{
    // 0 字节写入不处理，直接返回
    if (size == 0)
        return 0;
    // 参数校验：pipe 和 buf 均不能为空
    if (pipe == NULL || buf == NULL)
        return -1;
    // 进入临界区，防止多线程并发读写冲突
    uint32_t critical = osiEnterCritical();
    // 计算当前可写空间（写指针 - 读指针 = 已用空间）
    unsigned space = pipe->size - (pipe->wr - pipe->rd);
    // 最终写入长度为 可写空间 和 请求写入长度 中的较小者
    unsigned len = OSI_MIN(unsigned, size, space);
    // 保存当前写指针
    unsigned wr = pipe->wr;

    // 如果管道已关闭或写入端被设置为 EOF，写入失败
    if (!pipe->running || pipe->eof)
    {
        osiExitCritical(critical);
        return -1;
    }
    // 若没有足够空间，则本次不写入数据
    if (len == 0)
    {
        osiExitCritical(critical);
        return 0;
    }
    // 计算写入位置在缓冲区中的偏移
    unsigned offset = wr % pipe->size;
    // tail 表示从当前 offset 到缓冲区末尾的剩余空间
    unsigned tail = pipe->size - offset;
    if (tail >= len)
    {
        // 一次 memcpy 写入（不会发生缓冲区环绕）
        memcpy(&pipe->data[offset], buf, len);
    }
    else
    {
        // 分成两段写入：先写尾部，再从头写剩余部分（环绕情况）
        memcpy(&pipe->data[offset], buf, tail);
        memcpy(pipe->data, (const char *)buf + tail, len - tail);
    }
    // 更新写指针
    pipe->wr += len;
    // 退出临界区
    osiExitCritical(critical);

    /**
     * 问题:既然已经使用 回调函数 通知了读端新数据到达，为什么还需要 信号量通知？
     * 答复：回调函数用于事件驱动模型，信号量用于线程同步模型。二者并不冲突，而是面向不同使用场景设计的
     * 1. 信号量用于阻塞式读取（同步等待模型）
     * 2. 回调用于事件驱动式读取（异步/通知模型）
     */

    // 如果有写入成功
    if (len > 0)
    {
        // 若已注册读端回调，且回调事件包含“数据到达”
        if (pipe->rd_cb != NULL && (pipe->rd_cb_mask & OSI_PIPE_EVENT_RX_ARRIVED))
            pipe->rd_cb(pipe->rd_cb_ctx, OSI_PIPE_EVENT_RX_ARRIVED);
    }
    // 通知读端有新数据可读
    osiSemaphoreRelease(pipe->rd_avail_sema);
    // 返回实际写入的长度
    return len;
}

int osiPipeReadAll(osiPipe_t *pipe, void *buf, unsigned size, unsigned timeout)
{
    if (size == 0)
        return 0;
    if (pipe == NULL || buf == NULL)
        return -1;

    unsigned len = 0;
    osiElapsedTimer_t timer;
    osiElapsedTimerStart(&timer);
    for (;;)
    {	
        int bytes = osiPipeRead(pipe, buf, size);
        if (bytes < 0)
            return -1;

        len += bytes;
        size -= bytes;
        buf = (char *)buf + bytes;
        if (size == 0 || timeout == 0 || pipe->eof)
            break;

        if (timeout == OSI_WAIT_FOREVER)
        {
            osiSemaphoreAcquire(pipe->rd_avail_sema);
        }
        else
        {
            int wait = timeout - osiElapsedTime(&timer);
            if (wait < 0 || !osiSemaphoreTryAcquire(pipe->rd_avail_sema, wait))
                break;
        }
    }

    return len;
}

int osiPipeWriteAll(osiPipe_t *pipe, const void *buf, unsigned size, unsigned timeout)
{
    if (size == 0)
        return 0;
    if (pipe == NULL || buf == NULL)
        return -1;

    unsigned len = 0;
    osiElapsedTimer_t timer;
    osiElapsedTimerStart(&timer);
    for (;;)
    {
        int bytes = osiPipeWrite(pipe, buf, size);
        if (bytes < 0)
            return -1;

        len += bytes;
        size -= bytes;
        buf = (const char *)buf + bytes;
        if (size == 0 || timeout == 0)
            break;

        if (timeout == OSI_WAIT_FOREVER)
        {
            osiSemaphoreAcquire(pipe->wr_avail_sema);
        }
        else
        {
            int wait = timeout - osiElapsedTime(&timer);
            if (wait < 0 || !osiSemaphoreTryAcquire(pipe->wr_avail_sema, wait))
                break;
        }
    }

    return len;
}

int osiPipeReadAvail(osiPipe_t *pipe)
{
    if (pipe == NULL)
        return -1;

    uint32_t critical = osiEnterCritical();
    unsigned bytes = pipe->wr - pipe->rd;
    osiExitCritical(critical);
    return bytes;
}

int osiPipeWriteAvail(osiPipe_t *pipe)
{
    if (pipe == NULL)
        return -1;

    uint32_t critical = osiEnterCritical();
    unsigned space = pipe->size - (pipe->wr - pipe->rd);
    osiExitCritical(critical);
    return space;
}

bool osiPipeWaitReadAvail(osiPipe_t *pipe, unsigned timeout)
{
    if (pipe == NULL)
        return false;

    osiElapsedTimer_t timer;
    osiElapsedTimerStart(&timer);
    for (;;)
    {
        if (!pipe->running)
            return false;

        if (osiPipeReadAvail(pipe) > 0)
            return true;

        if (pipe->eof)
            return false;

        if (timeout == OSI_WAIT_FOREVER)
        {
            osiSemaphoreAcquire(pipe->rd_avail_sema);
        }
        else
        {
            int wait = timeout - osiElapsedTime(&timer);
            if (wait < 0 || !osiSemaphoreTryAcquire(pipe->rd_avail_sema, wait))
                return false;
        }
    }

    // never reach
}

bool osiPipeWaitWriteAvail(osiPipe_t *pipe, unsigned timeout)
{
    if (pipe == NULL)
        return false;

    osiElapsedTimer_t timer;
    osiElapsedTimerStart(&timer);
    for (;;)
    {
        if (!pipe->running)
            return false;

        if (osiPipeWriteAvail(pipe) > 0)
            return true;

        if (timeout == OSI_WAIT_FOREVER)
        {
            osiSemaphoreAcquire(pipe->wr_avail_sema);
        }
        else
        {
            int wait = timeout - osiElapsedTime(&timer);
            if (wait < 0 || !osiSemaphoreTryAcquire(pipe->wr_avail_sema, wait))
                return false;
        }
    }

    // never reach
}
