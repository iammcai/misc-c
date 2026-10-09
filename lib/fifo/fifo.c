/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    fifo.h
 * @brief   FIFO环形缓冲实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-10-9
 * @version 1.0
 *
 * @note    未加锁，仅限单线程使用
 *
 * @history
 *   1.0 | 2026-10-9 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "fifo/fifo.h"
#include "mp/mp.h"
#include "plat/debug.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// fifo结构定义
struct fifo_s{
    size_t head;
    size_t tail;
    size_t size;        // buffer size
    char buffer[];      // actual buffer
};

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       get fifo remian size free
 * 
 * @param[in]   fifo
 * 
 * @retval      free size of fifo
 */
static attr_pure_inline size_t _fifo_remain(fifo_t *fifo)
{
    if(!fifo || fifo->size == 0)
        return 0;
    return fifo->size - (fifo->tail - fifo->head);
}

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

fifo_t* fifo_create(size_t size)
{
    // 要求size必须是2次幂
    pfm_ensure_ret(size > 0 && !(size & (size - 1)), NULL);

    fifo_t *fifo = mp_calloc(1, sizeof(fifo_t) + size);
    pfm_ensure_ret(fifo, NULL);

    fifo->size = size;

    return fifo;
}

void fifo_destroy(fifo_t **fifo)
{
    if(fifo && *fifo)
    {
        mp_free(*fifo, sizeof(fifo_t) + (*fifo)->size);
        *fifo = NULL;
    }
}

size_t fifo_write(fifo_t *fifo, const void *src, size_t len)
{
    pfm_ensure_ret(fifo && src && len, 0);

    // 计算可写入长度
    size_t write_size = len;
    size_t remain = _fifo_remain(fifo);
    if(remain == 0)
        return 0;
    else if(remain < len)
        write_size = remain;

    size_t fifo_buf_mask = fifo->size - 1;
    size_t tail = fifo->tail & fifo_buf_mask;   // 真实尾指针

    size_t first_chunk = fifo->size - tail;         // tail到末尾还有多少长度
    if(first_chunk >= write_size)                       // 足够一次性写入
        memcpy(fifo->buffer + tail, src, write_size);
    else        // 分两段
    {
        memcpy(fifo->buffer + tail, src, first_chunk);
        memcpy(fifo->buffer, src + first_chunk, write_size - first_chunk);
    }

    fifo->tail += write_size;

    return write_size;
}

size_t fifo_read(fifo_t *fifo, char *dest, size_t len, int out)
{
    pfm_ensure_ret(fifo && dest && len, 0);

    // 获取当前有效数据长度
    size_t valid_size = fifo->tail - fifo->head;
    if(len > valid_size)
        len = valid_size;
    if(len == 0)
        return 0;

    // 获取真实head
    size_t fifo_buf_mask = fifo->size - 1;
    size_t head = fifo->head & fifo_buf_mask;

    size_t first_chunk = fifo->size - head;     // head到缓冲区末尾有多少长度
    if(first_chunk >= len)
        memcpy(dest, fifo->buffer + head, len);
    else        // 分段
    {
        memcpy(dest, fifo->buffer + head, first_chunk);
        memcpy(dest + first_chunk, fifo->buffer, len - first_chunk);
    }

    if(out)     // 是否移除出fifo
        fifo->head += len;

    return len;
}

size_t fifo_get_used(fifo_t *fifo)
{
    return !fifo ? 0 : fifo->tail - fifo->head;
}