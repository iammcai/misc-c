/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    uds.h
 * @brief   ipc-unix_dgram_socket头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-09-18
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-09-18 | cai | Initial creation.
 */

#ifndef __UDS_H__
#define __UDS_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "plat/compiler.h"
#include "plat/debug.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// uds管理结构声明
typedef struct uds_mgnt_s uds_mgnt_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       create new uds mgnt
 * 
 * @param[in]   sock_path   - unix socket path
 * @param[in]   is_recv     - if receiver
 * 
 * @retval      ptr to uds mgnter
 */
extern uds_mgnt_t* uds_mgnt_create(const char *sock_path, int is_recv);

/**
 * @brief       destroy uds mgnter
 */
extern void uds_mgnt_destroy(uds_mgnt_t *uds_mgnt);

/**
 * @brief       send by uds
 * 
 * @param[in]   uds_mgnt        - uds mgnt
 * @param[in]   data            - data
 * @param[in]   len             - data len
 * 
 * @retval      error code
 */
extern error_code_e uds_send(uds_mgnt_t *uds_mgnt, const void *data, size_t len);

/**
 * @brief       recv by uds, block
 * 
 * @param[in]   uds_mgnt        - uds mgnt
 * @param[in]   buf             - buffer
 * @param[in]   buf_size        - size of buffer
 * 
 * @retval      receive data len
 */
extern ssize_t uds_recv(uds_mgnt_t *uds_mgnt, void *buf, size_t buf_size);

/**
 * @brief       try recv by uds, unblock
 * 
 * @param[in]   uds_mgnt        - uds mgnt
 * @param[in]   buf             - buffer
 * @param[in]   buf_size        - size of buffer
 * 
 * @retval      receive data len
 */
extern ssize_t uds_try_recv(uds_mgnt_t *uds_mgnt, void *buf, size_t buf_size);

#endif
/* __UDS_H__ end */