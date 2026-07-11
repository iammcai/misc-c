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
#include "cli/cli.h"
#include <string.h>

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       cli hook for: debug level set <level>
 * 
 * @param[in]   argc    - 1
 * @param[in]   argv[0] - debug level
 */
static attr_force_inline void* cli_set_debug_level_hook(unsigned char argc, char* argv[]);

/**
 * @brief       cli hook for: debug level get
 */
static attr_pure_inline void* cli_get_debug_level_hook(unsigned char argc, char* argv[]);

/**
 * @brief       debug early init
 * 
 * @note        register cli
 */
static void _debug_init() attr_ctor(CTOR_PRIO_MID);

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// dbg最大长度
#define DBG_LEN_MAX     (256)

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

// 全局调试等级
static ATOMIC_UINT8_T g_debug_level = debug_level_none;
// 全局互斥锁
static ev_mutex_t g_debug_mtx = EV_MUTEX_INITIALIZER;
// level - name 对照表
const char *level_name_table[debug_level_cnt] = {
    "none",
    "normal",
    "major",
    "error",
    "always",
    "all"
};

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
            fmt = "[%.3f] [%s] [%s:%d %s] %s\n";    // [time] [level] [file:line func] usr_fmt
            break;
        case debug_level_error:
            fmt = "[%.3f] [%s] [%s:%d %s] " fmt_color_yellow "%s\n" fmt_color_clear;
            break;
        case debug_level_major:
            fmt = "[%.3f] [%s] [%s:%d %s] " fmt_color_red "%s\n" fmt_color_clear;
            break;
        case debug_level_always:
            fmt = "[%.3f] [%s] [%s:%d %s] " fmt_color_green "%s\n" fmt_color_clear;
            break;
        default:
            return;
    }

    // 获取时间
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

    va_start(list, usr_fmt);
    snprintf(dbg_fmt, DBG_LEN_MAX, fmt, 
        (double)ts.tv_sec + (double)ts.tv_nsec/1000000000, 
        level_name_table[level], file, line, func, usr_fmt
    );
    ev_with_mutex(&g_debug_mtx)
        vprintf(dbg_fmt, list);     // 输出

    va_end(list);
}

void _safe_printf(const char *fmt, ...)
{
    va_list list;
    va_start(list, fmt);
    ev_with_mutex(&g_debug_mtx)
        vprintf(fmt, list);     // 输出

    va_end(list);
}

void debug_level_set(debug_level_e level)
{
    if(level == debug_level_always)
        return;

    ATOM_STORE(&g_debug_level, level, MORDER_RELEASE);
}

static attr_force_inline void* cli_set_debug_level_hook(unsigned char argc, char* argv[])
{
    if(argc != 1)
    {
        safe_printf("Error param count\n");
        return NULL;
    }

    unsigned char i = 0;
    for(; i < debug_level_cnt; ++ i)
    {
        if(!strcmp(level_name_table[i], argv[0]))
            break;
    }
    if(i == debug_level_cnt)
    {
        safe_printf("Error debug level: %s\n", argv[0]);
        return NULL;
    }

    debug_level_set(i);
    
    return NULL;
}

static inline void* cli_get_debug_level_hook(unsigned char argc, char* argv[])
{
    safe_printf("%s\n", level_name_table[ATOM_LOAD(&g_debug_level, MORDER_ACQUIRE)]);

    return NULL;
}

static void _debug_init()
{
    cli_param_t set_param[] = {
        {
            .short_name = '\0',
            .type = PARAM_POS,
            .required = 1,
            .help = "support: none / normal / major / error / all",
        },
    };

    cli_register("debug level set", "set debug level", set_param, cli_set_debug_level_hook);
    cli_register("debug level get", "get debug level", NULL, cli_get_debug_level_hook);
}