/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    shm.h
 * @brief   ipc-share memory 共享内存头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-10-8
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-10-8 | cai | Initial creation.
 */

#ifndef __SHM_H__
#define __SHM_H__

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

// 声明共享内存类型
typedef struct shm_mgnt_s shm_mgnt_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       open shm
 * 
 * @param[in]   name    - shm name, format: "/xxx"
 * @param[in]   size    - size of shm
 * @param[in]   is_creater
 * 
 * @retval      ptr to shm
 */
extern shm_mgnt_t* shm_mgnt_open(const char *name, size_t size, int is_creater);

/**
 * @brief       close shm
 * 
 * @param[in]   shm
 * 
 * @retval      error code
 */
extern error_code_e shm_mgnt_close(shm_mgnt_t *shm);

/**
 * @brief       write shm
 * 
 * @param[in]   shm
 * @param[in]   offset      - offset
 * @param[in]   data
 * @param[in]   len         - data len
 * 
 * @retval      error code
 */
extern error_code_e shm_mgnt_write(shm_mgnt_t *shm, size_t offset, const void *data, size_t len);

/**
 * @brief       read shm
 * 
 * @param[in]   shm
 * @param[in]   offset
 * @param[in]   buf     - buffer
 * @param[in]   size    - buffer len
 * 
 * @retval      error code
 */
extern error_code_e shm_mgnt_read(shm_mgnt_t *shm, size_t offset, void *buf, size_t len);

#endif
/* __SHM_H__ end */