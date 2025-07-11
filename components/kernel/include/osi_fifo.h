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

#ifndef _OSI_FIFO_H_
#define _OSI_FIFO_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 通用环形 FIFO 缓冲区结构体
 *
 * 本结构体用于实现无锁或轻量锁保护的**环形缓冲区（FIFO）**机制，适用于任务间通信、
 * 串口接收缓存、日志缓冲、音频数据传输等应用场景。
 *
 * - 数据以字节流形式存储在外部分配的缓冲区 `data` 中
 * - 使用两个指针 `rd` 和 `wr` 分别指示读写位置（逻辑索引）
 * - 数据存储空间大小由 `size` 字段指定，**不包含结构体本身**
 *
 * @note 本结构体本身不包含并发保护，需由调用者在多线程场景中加锁
 */
typedef struct osiFifo
{
    void *data;  ///< FIFO 缓冲区首地址（调用者分配）
    size_t size; ///< FIFO 缓冲区大小（单位：字节）
    size_t rd;   ///< 当前读取位置指针（逻辑位置，不是实际地址）
    size_t wr;   ///< 当前写入位置指针（逻辑位置，不是实际地址）
} osiFifo_t;

/**
 * \brief initialize FIFO
 *
 * \param fifo      the FIFO pointer
 * \param data      FIFO buffer
 * \param size      FIFO buffer size
 * \return
 *      - true on success
 *      - false on invalid parameter
 */
bool osiFifoInit(osiFifo_t *fifo, void *data, size_t size);

/**
 * \brief reset FIFO
 *
 * After reset, the internal state indicates there are no data in the
 * FIFO.
 *
 * \param fifo      the FIFO pointer
 */
void osiFifoReset(osiFifo_t *fifo);

/**
 * \brief put data into FIFO
 *
 * The returned actual put size may be less than \a size.
 *
 * \param fifo      the FIFO pointer
 * \param data      data to be put into FIFO
 * \param size      data size
 * \return      actually put size
 */
int osiFifoPut(osiFifo_t *fifo, const void *data, size_t size);

/**
 * \brief get data from FIFO
 *
 * The returned actual get size may be less than \a size.
 *
 * \param fifo      the FIFO pointer
 * \param data      data buffer for get
 * \param size      data buffer size
 * \return      actually get size
 */
int osiFifoGet(osiFifo_t *fifo, void *data, size_t size);

/**
 * \brief peek data from FIFO
 *
 * On peek, the read position of FIFO won't be updated.
 *
 * \param fifo      the FIFO pointer
 * \param data      data buffer for peek
 * \param size      data buffer size
 * \return      actually peek size
 */
int osiFifoPeek(osiFifo_t *fifo, void *data, size_t size);

/**
 * \brief update read position to skip bytes in FIFO
 *
 * When \a size is larger than byte count in FIFO, only available bytes will
 * be skipped.
 *
 * \param fifo      the FIFO pointer
 * \param size      byte count to be skipped
 */
void osiFifoSkipBytes(osiFifo_t *fifo, size_t size);

/**
 * \brief search a byte in FIFO
 *
 * At search, the non-matching bytes will be dropped from the FIFO.
 * When \a byte is found, it is configurable to keep the byte or
 * drop the byte.
 *
 * \param fifo      the FIFO pointer
 * \param byte      the byte to be searched
 * \param keep      true to keep the found byte, false to drop the found byte
 * \return
 *      - true if \a byte is found
 *      - false if \a byte is not found
 */
bool osiFifoSearch(osiFifo_t *fifo, uint8_t byte, bool keep);

/**
 * \brief byte count in the FIFO
 *
 * \param fifo      the FIFO pointer
 * \return      the byte count of the FIFO
 */
static inline size_t osiFifoBytes(osiFifo_t *fifo) { return fifo->wr - fifo->rd; }

/**
 * \brief available space in the FIFO
 *
 * \param fifo      the FIFO pointer
 * \return      the available space byte count of the FIFO
 */
static inline size_t osiFifoSpace(osiFifo_t *fifo) { return fifo->size - osiFifoBytes(fifo); }

/**
 * \brief chech whether the FIFO is full
 *
 * \param fifo      the FIFO pointer
 * \return
 *      - true if the FIFO is full
 *      - false if the FIFO is not full
 */
static inline bool osiFifoIsFull(osiFifo_t *fifo) { return osiFifoSpace(fifo) == 0; }

/**
 * \brief chech whether the FIFO is empty
 *
 * \param fifo      the FIFO pointer
 * \return
 *      - true if the FIFO is empty
 *      - false if the FIFO is not empty
 */
static inline bool osiFifoIsEmpty(osiFifo_t *fifo) { return osiFifoBytes(fifo) == 0; }

#ifdef __cplusplus
}
#endif
#endif
