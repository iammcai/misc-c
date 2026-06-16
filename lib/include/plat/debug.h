/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    debug.h
 * @brief   调试文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-09
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-09 | cai | Initial creation.
 */

#ifndef __DEBUG_H__
#define __DEBUG_H__

 /* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "plat/compiler.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 调试等级枚举
typedef enum{
    debug_level_none = 0,       // 不打印
    debug_level_normal,         // 普通等级
    debug_level_major,          // 关键信息
    debug_level_error,          // 错误信息
    debug_level_always,         // 永远打印的信息
    debug_level_all,            // 输出所有信息

    debug_level_cnt,
}debug_level_e;

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 格式化输出控制
#define fmt_color_red           "\033[31m"
#define fmt_color_green         "\033[32m"
#define fmt_color_yellow        "\033[33m"
#define fmt_color_clear         "\033[0m"

/**
 * 外部使用，区分调试等级
 */
#define dbg(fmt, args...)   \
    _debug_printf(debug_level_normal, __FILE_NAME__, __func__, __LINE__, fmt, ##args);   \
/* dbg end */
#define dbg_major(fmt, args...) \
    _debug_printf(debug_level_major, __FILE_NAME__, __func__, __LINE__, fmt, ##args);   \
/* dbg end */
#define dbg_error(fmt, args...)     \
    _debug_printf(debug_level_error, __FILE_NAME__, __func__, __LINE__, fmt, ##args);   \
/* dbg end */
#define dbg_always(fmt, args...)    \
    _debug_printf(debug_level_always, __FILE_NAME__, __func__, __LINE__, fmt, ##args);   \
/* dbg end */

/**
 * 外部使用，安全打印
 */
#define safe_printf(fmt, args...)   \
    _safe_printf(fmt, ##args)

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       debug printf
 * 
 * @param[in]   level   - debug level
 * @param[in]   file    - file name
 * @param[in]   func    - function name
 * @param[in]   line    - line number
 * @param[in]   usr_fmt - printf format
 * @param[in]   ...     - args
 * 
 */
extern void _debug_printf(debug_level_e level, const char *file, const char *func, int line, const char *usr_fmt, ...);

/**
 * @brief       set debug level
 * 
 * @param[in]   level   - debug level, none, normal, maj, err, all
 */
extern void debug_level_set(debug_level_e level);

/**
 * @brief       safe printf
 * 
 * @param[in]   fmt - format
 * @param[in]   ... - other args
 */
extern void _safe_printf(const char *fmt, ...);

#endif