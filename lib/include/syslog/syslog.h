/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    syslog.h
 * @brief   系统日志头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-07-07
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-07-07 | cai | Initial creation.
 */

#ifndef __SYSLOG_H__
#define __SYSLOG_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "plat/compiler.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 日志严重等级枚举
typedef enum{
    SYSLOG_FACILITY_EMERG = 0,
    SYSLOG_FACILITY_ALERT,
    SYSLOG_FACILITY_CRIT,
    SYSLOG_FACILITY_ERR,
    SYSLOG_FACILITY_WARN,
    SYSLOG_FACILITY_NOTICE,
    SYSLOG_FACILITY_INFO,
    SYSLOG_FACILITY_DEBUG,

    SYSLOG_FACILITY_MAX,        // 仅用于计数
}syslog_facility_e;

// 日志模块id枚举
typedef enum{
    SYSLOG_MODULE_SYS = 0,

    SYSLOG_MODULE_MAX,          // 计数
}syslog_module_e;

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

// 日志模块对应字符串
static const char* syslog_module_str[SYSLOG_MODULE_MAX] = {
    "system",
};

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/**
 * 内部使用，添加日志
 */
#define _syslog_add(module, facility, ctx)  \
    _syslog_entry_push(module, facility, ctx);  \
/* syslog_add end */

/**
 * 外部使用，添加紧急日志
 */
#define syslog_emerg(module, ctx)   \
    _syslog_add(module, SYSLOG_FACILITY_EMERG, ctx) \
/* syslog_emerg end */

/**
 * 外部使用，添加警报日志
 */
#define syslog_alert(module, ctx)   \
    _syslog_add(module, SYSLOG_FACILITY_ALERT, ctx) \
/* syslog_alert end */

/**
 * 外部使用，添加严重日志
 */
#define syslog_crit(module, ctx)    \
    _syslog_add(module, SYSLOG_FACILITY_CRIT, ctx)  \
/* syslog_crit end */

/**
 * 外部使用，添加错误日志
 */
#define syslog_err(module, ctx) \
    _syslog_add(module, SYSLOG_FACILITY_ERR, ctx)   \
/* syslog_err end */

/**
 * 外部使用，添加警告日志
 */
#define syslog_warn(module, ctx)    \
    _syslog_add(module, SYSLOG_FACILITY_WARN, ctx)  \
/* syslog_warn end */

/**
 * 外部使用，添加通知日志
 */
#define syslog_notice(module, ctx)  \
    _syslog_add(module, SYSLOG_FACILITY_NOTICE, ctx)    \
/* syslog_notice end */

/**
 * 外部使用，添加信息日志
 */
#define syslog_info(module, ctx)    \
    _syslog_add(module, SYSLOG_FACILITY_INFO, ctx)  \
/* syslog_info end */

/**
 * 外部使用，添加调试日志
 */
#define syslog_debug(module, ctx)   \
    _syslog_add(module, SYSLOG_FACILITY_DEBUG, ctx) \
/* syslog_debug end */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       push syslog entry into msg_q
 * 
 * @param[in]   module  - mod id
 * @param[in]   facility    - log facility
 * @param[in]   ctx     - log context
 */
extern void _syslog_entry_push(syslog_module_e module, syslog_facility_e facility, const char *ctx);

#endif