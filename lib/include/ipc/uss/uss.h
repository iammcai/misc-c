/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    uss.h
 * @brief   ipc-unix_stream_socket头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-09-28
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-09-28 | cai | Initial creation.
 */

#ifndef __USS_H__
#define __USS_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "plat/compiler.h"
#include "plat/debug.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// uss管理结构声明
typedef struct uss_mgnt_s uss_mgnt_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       get fd of uss mgnter
 * 
 * @param[in]   uss_mgnter
 * 
 * @retval      fd, -1 while fail
 */
extern int uss_mgnter_fd_get(const uss_mgnt_t *uss_mgnter);

/**
 * @brief       创建unix stream socket
 * 
 * @param[in]   sock_path       - socket path
 * @param[in]   is_receiver     - 是否接收端
 * @param[in]   nonblock        - if non blocking
 * 
 * @retval      uss mgnter
 */
extern uss_mgnt_t* uss_mgnter_create(const char *sock_path, int is_receiver, int nonblock);

/**
 * @brief       接受一个新的客户端连接，server用
 * 
 * @param[in]   uss_receiver    - uss receiver mgnter
 * @param[in]   nonblock        - if non blocking
 * 
 * @retval      new uss mgnter
 */
extern uss_mgnt_t* uss_mgnter_accept(uss_mgnt_t *uss_receiver, int nonblock);

/**
 * @brief       销毁uss mgnter
 * 
 * @param[in]   uss_mgnter
 */
extern void uss_mgnter_destory(uss_mgnt_t *uss_mgnter);

/**
 * @brief       uss send
 * 
 * @param[in]   uss_mgnter
 * @param[in]   data
 * @param[in]   len         - length of data
 * 
 * @retval      error code
 */
extern error_code_e uss_send(uss_mgnt_t *uss_mgnter, const void *data, size_t len);

/**
 * @brief       receive by uss, blocking
 * 
 * @param[in]   uss_mgnter
 * @param[out]  buf         - buffer
 * @param[in]   buf_size    - size of buffer
 * 
 * @retval      receive size
 */
extern ssize_t uss_recv(uss_mgnt_t *uss_mgnter, void *buf, size_t buf_size);

#endif
/* __USS_H__ end */