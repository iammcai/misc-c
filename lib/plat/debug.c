/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    debug.c
 * @brief   调试实现
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

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdarg.h>
#include <stdio.h>
#include "plat/debug.h"
#include "plat/atom.h"
#include "event/ev_lock.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// dbg最大长度
#define DBG_LEN_MAX     (256)

// 格式化输出控制
#define fmt_color_red           "\033[31m"
#define fmt_color_green         "\033[32m"
#define fmt_color_yellow        "\033[33m"
#define fmt_color_clear         "\033[0m"

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

// 全局调试等级
static ATOMIC_UINT8_T g_debug_level = debug_level_none;
// 全局互斥锁
static ev_mutex_t g_debug_mtx = EV_MUTEX_INITIALIZER;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void _debug_printf(debug_level_e level, const char *file, const char *func, int line, const char *usr_fmt, ...)
{
    debug_level_e cur_level = ATOM_LOAD(&g_debug_level, MORDER_ACQUIRE);
    va_list list;
    char dbg_fmt[DBG_LEN_MAX] = {};

    if(cur_level == debug_level_none && level != debug_level_always)
        return;
    else if(cur_level == debug_level_normal && level != debug_level_normal)
        return;
    else if(cur_level == debug_level_major && level != debug_level_major)
        return;
    else if(cur_level == debug_level_error && level != debug_level_error)
        return;

    const char *fmt = NULL;

    switch(level)
    {
        case debug_level_normal:
            fmt = "<%s | %s | %d> %s\n";    // <file | func | line> fmt
            break;
        case debug_level_error:
            fmt = fmt_color_yellow "<%s | %s | %d> %s\n" fmt_color_clear;
            break;
        case debug_level_major:
            fmt = fmt_color_green "<%s | %s | %d> %s\n" fmt_color_clear;
            break;
        case debug_level_always:
            fmt = fmt_color_red "<%s | %s | %d> %s\n" fmt_color_clear;
            break;
        default:
            return;
    }

    va_start(list, usr_fmt);
    snprintf(dbg_fmt, DBG_LEN_MAX, fmt, file, func, line, usr_fmt);
    ev_with_mutex(&g_debug_mtx)
    {
        vprintf(dbg_fmt, list);     // 输出
    }
    va_end(list);
}

void debug_level_set(debug_level_e level)
{
    if(level == debug_level_always)
        return;

    ATOM_STORE(&g_debug_level, level, MORDER_RELEASE);
}