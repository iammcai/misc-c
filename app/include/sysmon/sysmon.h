/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    sysmon.h
 * @brief   系统监控头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-08-12
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-08-12 | cai | Initial creation.
 */

#ifndef __SYSMON_H__
#define __SYSMON_H__

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

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       http cgi response for /api/sysmon/cpu/usage
 */
extern error_code_e sysmon_cpu_usage_http_cgi_cb(char *buf, size_t buf_size);

/**
 * @brief       get total cpu usage
 * 
 * @param[out]  usage   - total cpu usage
 */
extern void sysmon_cpu_usage_get(double *usage);

/**
 * @brief       cli hook for show cpu usage
 */
extern void sysmon_show_cpu_usage_cli_hook(unsigned char argc, char *argv[]);

/**
 * @brief       init sysmon module
 */
extern void sysmon_module_init();

/**
 * @brief       exit sysmon module
 */
extern void sysmon_module_exit();

#endif