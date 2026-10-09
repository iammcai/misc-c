/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    fifo.h
 * @brief   FIFO环形缓冲头文件
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

#ifndef __FIFO_H__
#define __FIFO_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stddef.h>

#include "plat/compiler.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// fifo类型声明
typedef struct fifo_s fifo_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       create a fifo
 * 
 * @param[in]   size    - buffer size
 * 
 * @retval      ptr to fifo
 */
extern fifo_t* fifo_create(size_t size);

/**
 * @brief       destroy a fifo
 * 
 * @param[in]   fifo    - 2nd ptr to fifo
 */
extern void fifo_destroy(fifo_t **fifo);

/**
 * @brief       copy to fifo
 * 
 * @param[in]   fifo
 * @param[in]   src
 * @param[in]   len
 * 
 * @retval      copy size actually
 */
extern size_t fifo_write(fifo_t *fifo, const void *src, size_t len);

/**
 * @brief       read from fifo
 * 
 * @param[in]   fifo
 * @param[in]   dest
 * @param[in]   len             - dest buf size
 * @param[in]   out             - manba out or not
 * 
 * @retval      actual read size
 */
extern size_t fifo_read(fifo_t *fifo, char *dest, size_t len, int out);

/**
 * @brief       get fifo used size
 * 
 * @param[in]   fifo
 * 
 * @retval      used size
 */
extern size_t fifo_get_used(fifo_t *fifo);

#endif