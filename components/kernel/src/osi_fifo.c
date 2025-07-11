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

#include "osi_fifo.h"
#include "osi_api.h"
#include <string.h>
#include <stdlib.h>

/**
 * @brief 初始化 FIFO 缓冲区
 *
 * 该函数用于初始化一个 `osiFifo_t` 类型的环形缓冲区（FIFO），将指定的缓冲区 `data` 与
 * FIFO 结构体 `fifo` 关联，并将读写指针清零。
 *
 * - 不负责分配实际缓冲区内存，仅绑定外部提供的缓冲区
 * - 初始化完成后，可配合读写函数进行数据收发
 *
 * @param fifo  指向 FIFO 控制结构体的指针
 * @param data  指向实际缓冲区内存的指针（由调用者分配）
 * @param size  缓冲区大小（以字节为单位）
 * @return true 初始化成功；false 参数无效（空指针或大小为 0）
 *
 * @note 此函数为轻量初始化，无并发保护。如在多线程环境中使用，请自行加锁
 */
bool osiFifoInit(osiFifo_t *fifo, void *data, size_t size)
{
    // 判断参数合法性：结构体指针、数据缓冲区指针不能为空，大小不能为 0
    if (fifo == NULL || data == NULL || size == 0)
        return false;
     // 绑定实际缓冲区内存
    fifo->data = data;
    // 设置缓冲区大小
    fifo->size = size;
    // 初始化读写指针为 0，表示为空状态
    fifo->rd = 0;
    fifo->wr = 0;
    // 返回初始化成功
    return true;
}
/**
 * @brief 向 FIFO 写入数据（非阻塞）
 *
 * 将指定长度的数据从 `data` 写入 FIFO 环形缓冲区 `fifo` 中。写入过程带有并发保护，适用于中断和任务上下文。
 * 若可用空间不足，只写入部分数据。
 *
 * @param fifo  指向 FIFO 控制结构体的指针
 * @param data  待写入的数据指针
 * @param size  要写入的字节数
 * @return 实际写入的字节数，可能小于请求的 `size`
 *
 * @note 本函数为非阻塞写入，若 FIFO 剩余空间不足，将只写入一部分数据。
 */

int osiFifoPut(osiFifo_t *fifo, const void *data, size_t size)
{
    // 参数校验：空指针或写入长度为 0 时直接返回
    if (fifo == NULL || data == NULL || size == 0)
        return 0;

    // 进入临界区，防止多线程/中断并发访问 FIFO
    uint32_t cs = osiEnterCritical();

    // 获取当前 FIFO 剩余空间（可写入最大字节数）
    size_t len = osiFifoSpace(fifo);

    // 若可写空间大于 size，限制为 size；否则限制为 len
    if (len > size)
        len = size;

    // 当前写入位置在缓冲区中的偏移（wr 为线性写指针）
    size_t offset = fifo->wr % fifo->size;

    // 缓冲区尾部剩余空间
    size_t tail = fifo->size - offset;

    // 判断是否可以一次性写入（不需要回绕）
    if (tail >= len)
    {
        // 直接复制 len 字节
        memcpy((char *)fifo->data + offset, data, len);
    }
    else
    {
        // 需要回绕两段写入：先写尾部 tail，再写开头 len-tail
        memcpy((char *)fifo->data + offset, data, tail);
        memcpy(fifo->data, (char *)data + tail, len - tail);
    }

    // 更新写指针（逻辑写入长度）
    fifo->wr += len;

    // 退出临界区，允许其他线程或中断访问
    osiExitCritical(cs);

    // 返回实际写入的字节数
    return len;
}

/**
 * @brief 内部函数：从 FIFO 中预读（peek）数据但不移动读指针
 *
 * 此函数用于从 FIFO 中读取（拷贝）最多 `size` 字节的数据至 `data` 指向的缓存区，
 * 但不会更新 `fifo->rd` 读指针，因此不会影响 FIFO 状态。
 * 常用于调试、协议预解析、前置判读等场景。
 *
 * @param fifo  指向 FIFO 控制结构的指针
 * @param data  输出缓冲区指针（用于接收拷贝数据）
 * @param size  希望读取的最大字节数
 * @return 实际预读的数据字节数，范围为 [0, size]
 *
 * @note 本函数为内部使用函数（非线程安全），调用者应自行加锁或控制上下文安全。
 */

static int _peekInternal(osiFifo_t *fifo, void *data, size_t size)
{
    // 获取当前 FIFO 中已写入、待读取的数据字节数
    size_t len = osiFifoBytes(fifo);

    // 若请求读取长度超过实际数据长度，则限制为实际长度
    if (len > size)
        len = size;

    // 计算读指针在 FIFO 环形缓冲区中的偏移位置
    size_t offset = fifo->rd % fifo->size;

    // 缓冲区尾部剩余空间（从当前读位置开始）
    size_t tail = fifo->size - offset;

    // 如果尾部空间足够容纳全部数据
    if (tail >= len)
    {
        // 直接从 offset 位置读取 len 字节
        memcpy(data, (char *)fifo->data + offset, len);
    }
    else
    {
        // 分段读取：先读取尾部 tail 字节，再从头部读剩余的 len - tail
        memcpy(data, (char *)fifo->data + offset, tail);
        memcpy((char *)data + tail, fifo->data, len - tail);
    }

    // 返回实际读取（拷贝）的字节数
    return len;
}

/**
 * @brief 跳过（丢弃）FIFO 中指定数量的字节
 *
 * 该函数将 FIFO 的读指针向前移动 `size` 字节，相当于“跳过”这部分数据。
 * 如果 `size` 超过了当前可读数据的长度，则只跳过实际可读的部分。
 *
 * 常用于忽略无效数据或快速前移读指针（例如在协议解析中丢弃无效前导字节）。
 *
 * @param fifo 指向 FIFO 控制结构的指针
 * @param size 希望跳过的字节数
 *
 * @note 本函数是线程安全的，会在内部加锁（通过 critical section 实现）。
 */

void osiFifoSkipBytes(osiFifo_t *fifo, size_t size)
{
    // 检查参数合法性：FIFO 指针为空 或 请求跳过长度为 0，直接返回
    if (fifo == NULL || size == 0)
        return;

    // 进入临界区，防止读写指针被并发修改
    uint32_t sc = osiEnterCritical();

    // 获取当前 FIFO 中的可读字节数
    size_t len = osiFifoBytes(fifo);

    // 实际跳过的长度为 min(请求长度, 可读长度)
    if (len > size)
        len = size;

    // 将读指针向前推进，跳过指定字节
    fifo->rd += len;

    // 退出临界区，恢复中断或调度
    osiExitCritical(sc);
}

/**
 * @brief 从 FIFO 中读取数据
 *
 * 从 FIFO 结构中读取最多 `size` 字节的数据到 `data` 指向的缓冲区中，并将读指针推进。
 * 如果当前可读数据不足 `size` 字节，则只读取可用的部分。
 *
 * @param fifo 指向 FIFO 缓冲区结构的指针
 * @param data 指向用于存储读取数据的缓冲区
 * @param size 请求读取的数据字节数
 * @return 实际读取的字节数；失败时返回 0（如参数为空）
 *
 * @note 此函数是线程安全的，内部已进入临界区。
 * @note 若 size = 0，则直接返回 0，不做任何操作。
 */

int osiFifoGet(osiFifo_t *fifo, void *data, size_t size)
{
    // 参数校验：如果 fifo、data 为空，或 size 为 0，则无效，返回 0
    if (fifo == NULL || data == NULL || size == 0)
        return 0;

    // 进入临界区，防止 FIFO 被其他线程读写操作破坏
    uint32_t cs = osiEnterCritical();

    // 调用内部函数 _peekInternal：将 FIFO 中的数据复制到 data，但不修改读指针
    int len = _peekInternal(fifo, data, size);

    // 真正推进读指针，表示数据已被消费
    fifo->rd += len;

    // 退出临界区，恢复中断或任务调度
    osiExitCritical(cs);

    // 返回实际读取的数据长度
    return len;
}

/**
 * @brief 查看 FIFO 中的数据但不推进读指针（peek）
 *
 * 从 FIFO 中读取最多 `size` 字节的数据到 `data` 缓冲区中，但不会修改 FIFO 的读指针，
 * 因此该操作不会“消费”数据。常用于查看数据内容但保留数据。
 *
 * @param fifo 指向 FIFO 缓冲结构体
 * @param data 接收数据的缓冲区指针
 * @param size 想要读取的最大字节数
 * @return 实际读取的字节数，0 表示无数据或参数错误
 *
 * @note 与 osiFifoGet 的区别是此函数不会推进 `rd` 读指针。
 * @note 该函数是线程安全的，内部使用临界区保护。
 */

int osiFifoPeek(osiFifo_t *fifo, void *data, size_t size)
{
    // 参数检查：如果 fifo/data 为空 或 请求长度为 0，则无效，直接返回 0
    if (fifo == NULL || data == NULL || size == 0)
        return 0;

    // 进入临界区，防止被并发修改
    uint32_t cs = osiEnterCritical();

    // 使用内部 peek 函数读取数据，但不移动 rd（读指针）
    int len = _peekInternal(fifo, data, size);

    // 退出临界区，恢复系统调度/中断
    osiExitCritical(cs);

    // 返回实际读取的数据长度
    return len;
}

/**
 * @brief 重置 FIFO 缓冲区（清空数据）
 *
 * 将 FIFO 的读指针 `rd` 和写指针 `wr` 设为 0，相当于清空 FIFO 中的所有数据。
 * 不会释放或重新分配缓存空间，仅逻辑上重置。
 *
 * @param fifo 指向需重置的 FIFO 结构体指针，若为 NULL 则不执行任何操作。
 *
 * @note 此函数是线程安全的，内部使用临界区保护。
 * @note 重置后 FIFO 可重新使用。
 */

void osiFifoReset(osiFifo_t *fifo)
{
    // 检查参数合法性：仅在 fifo 非空时进行操作
    if (fifo != NULL)
    {
        // 进入临界区，防止读写指针在多线程环境中被并发访问
        uint32_t cs = osiEnterCritical();

        // 重置读指针和写指针，逻辑上清空 FIFO
        fifo->wr = fifo->rd = 0;

        // 退出临界区，恢复调度或中断
        osiExitCritical(cs);
    }
}

/**
 * @brief 在 FIFO 中查找指定字节并定位读指针
 *
 * 在 FIFO 中从当前读指针（rd）开始向前查找是否存在指定字节（byte）。
 * - 若找到：
 *   - 若 `keep == true`，读指针 `rd` 指向目标字节位置（保留匹配字节）；
 *   - 若 `keep == false`，读指针跳过目标字节。
 * - 若未找到：读指针直接追上写指针（清空缓冲）。
 *
 * @param fifo  指向 FIFO 结构体的指针
 * @param byte  要查找的字节值
 * @param keep  是否保留匹配到的字节（true: 保留；false: 丢弃）
 * @return true 如果找到目标字节，false 否则
 *
 * @note 该函数不修改 FIFO 内容，仅操作读指针。
 * @note 本函数未加锁，需调用方保证线程安全。
 */

bool osiFifoSearch(osiFifo_t *fifo, uint8_t byte, bool keep)
{
    // 判空保护：FIFO 指针为空直接返回 false
    if (fifo == NULL)
        return false;

    // 获取当前读指针偏移地址
    uint8_t *p = (uint8_t *)fifo->data + (fifo->rd % fifo->size);
    uint8_t *pstart = (uint8_t *)fifo->data;              // FIFO 起始地址
    uint8_t *pend = (uint8_t *)fifo->data + fifo->size;   // FIFO 结束地址
    size_t wr = fifo->wr;  // 保存写指针位置（不参与修改）

    // 遍历 FIFO 中当前未读的数据区段
    for (int n = fifo->rd; n < wr; n++)
    {
        uint8_t ch = *p++;  // 取出当前字节

        // 找到目标字节
        if (ch == byte)
        {
            // 根据 keep 参数决定新的读指针位置
            fifo->rd = keep ? n : n + 1;
            return true;
        }

        // 到达缓冲区末尾时循环回起始
        if (p == pend)
            p = pstart;
    }

    // 未找到目标字节，将 rd 推到 wr，表示数据已读完
    fifo->rd = wr;
    return false;
}

