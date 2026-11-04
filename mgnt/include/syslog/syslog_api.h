/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    syslog_api.h
 * @brief   syslog对外API头文件
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

#ifndef __SYSLOG_API_H__
#define __SYSLOG_API_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdint.h>
#include <time.h>

#include "plat/debug.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 共享内存，标记syslog运行状态
#define SYSLOG_PROCESS_RUNNING_SHM_NAME     "/syslog_proc_running_shm"

// 添加emerg等级日志
#define SYSLOG_EMERG(module, fmt, args...)      _syslog_add(SYSLOG_FACILITY_EMERG, module, fmt, ##args);
// 添加alert等级日志
#define SYSLOG_ALERT(module, fmt, args...)      _syslog_add(SYSLOG_FACILITY_ALERT, module, fmt, ##args);
// 添加critical等级日志
#define SYSLOG_CRIT(module, fmt, args...)       _syslog_add(SYSLOG_FACILITY_CRIT, module, fmt, ##args);
// 添加error等级日志
#define SYSLOG_ERR(module, fmt, args...)        _syslog_add(SYSLOG_FACILITY_ERR, module, fmt, ##args);
// 添加warn等级日志
#define SYSLOG_WARN(module, fmt, args...)       _syslog_add(SYSLOG_FACILITY_WARN, module, fmt, ##args);
// 添加notice等级日志
#define SYSLOG_NOTICE(module, fmt, args...)     _syslog_add(SYSLOG_FACILITY_NOTICE, module, fmt, ##args);
// 添加info等级日志
#define SYSLOG_INFO(module, fmt, args...)       _syslog_add(SYSLOG_FACILITY_INFO, module, fmt, ##args);
// 添加debug等级日志
#define SYSLOG_DEBUG(module, fmt, args...)      _syslog_add(SYSLOG_FACILITY_DEBUG, module, fmt, ##args);

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

// 日志模块ID枚举
typedef enum{
    SYSLOG_MODULE_ID_ZCAP,      // zcap
    SYSLOG_MODULE_ID_LED,       // led
    SYSLOG_MODULE_ID_SHT30,     // sht30

    SYSLOG_MODULE_ID_MAX,       // 计数
}syslog_module_id_e;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

// 

/**
 * @brief       add syslog entry
 * 
 * @note        client调用，内部rpc给syslog进程处理
 */
extern error_code_e _syslog_add(syslog_facility_e facility, syslog_module_id_e module_id, const char *fmt, ...);

/**
 * @brief       syslog process entry
 * 
 * @note        syslog进程入口
 */
extern void syslog_proc_entry();

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 日志模块字符串
static const char* syslog_module_str[SYSLOG_MODULE_ID_MAX] = {
    "zcap",
    "led",
    "sht30",
};

#endif