/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    syslog_internal.h
 * @brief   syslog内部头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-11-03
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-11-03 | cai | Initial creation.
 */

#ifndef __SYSLOG_INTERNAL_H__
#define __SYSLOG_INTERNAL_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdint.h>
#include <stddef.h>

#include "plat/debug.h"
#include "rpc/rpc.h"
#include "syslog/syslog_api.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 日志内容大小
#define SYSLOG_ENTRY_CTX_SIZE           (108)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 日志条目结构
typedef struct{
    struct timespec ts;                 // 时间戳，树莓派5上16B
    uint32_t module : 5;                // 模块
    uint32_t facility : 3;              // 等级，和模块共用4B
    char ctx[SYSLOG_ENTRY_CTX_SIZE];    // 日志内容
} attr_aligned(8) syslog_entry_t;
_Static_assert(sizeof(syslog_entry_t) == 128, "sizeof(syslog_entry_t) must be 128B");

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */
//

/**
 * @brief       rpc server func for "syslog_add_rpc_server"
 * 
 * @param[in]   in_param    - ctx is syslog entry
 */
extern error_code_e _syslog_add_rpc_server(rpc_in_param_t *in_param, rpc_out_param_t *out_param);

/**
 * @brief       rpc server func for "syslog_flush_rpc_server"
 */
extern error_code_e _syslog_flush_rpc_server(rpc_in_param_t *in_param, rpc_out_param_t *out_param);

/**
 * @brief       store syslog entry into buffer
 * 
 * @param[in]   entry   - entry
 * 
 * @note        日志存储在buffer中，退出丢失
 */
extern error_code_e _syslog_store_buffer(syslog_entry_t *entry);

/**
 * @brief       start timer for flush flash
 * 
 * @note        启动定时器用于定时将Buffer内的日志刷盘
 */
extern error_code_e _syslog_store_flash_timer_start();

/**
 * @brief       end timer for flush flash
 * 
 * @note        结束定时刷盘定时器
 */
extern void _syslog_store_flash_timer_end();

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 日志严重等级对应字符串
static const char* syslog_facility_str[SYSLOG_FACILITY_MAX] = {
    "emergency",
    "alert",
    "critical",
    "error",
    "warning",
    "notice",
    "information",
    "debug",
};

#endif