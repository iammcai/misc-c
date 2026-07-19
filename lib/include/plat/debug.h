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

// 错误码枚举
typedef enum{
    ERR_NO_ERROR = 0,               // 无错误
    ERR_BAD_PARAM,                  // 参数错误
    ERR_NO_MEM,                     // 内存不足

    // nt模块
    ERR_NT_START = 1000,            // net tool错误码起始
    ERR_NT_IF_ERROR,                // 接口错误
    ERR_NT_CIDR_INVALID,            // cidr地址错误
    ERR_NT_ARP_CACHE_NOT_EXIST,     // arp cache不存在
    ERR_NT_ARP_SEND_FAIL,           // 发送arp失败
    ERR_NT_PING_DMAC_UNKNOWN,       // ping目的mac未知
}error_code_e;

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
/* dbg_major end */
#define dbg_error(fmt, args...)     \
    _debug_printf(debug_level_error, __FILE_NAME__, __func__, __LINE__, fmt, ##args);   \
/* dbg_error end */
#define dbg_always(fmt, args...)    \
    _debug_printf(debug_level_always, __FILE_NAME__, __func__, __LINE__, fmt, ##args);   \
/* dbg_always end */

/**
 * 外部使用，安全打印
 */
#define safe_printf(fmt, args...)   \
    _safe_printf(fmt, ##args)

/**
 * 外部使用，带调试的运行宏
 */
// 检查cond，不成立则dbg_err
#define pfm_ensure_dbg(cond) do{    \
    if(!(cond)) {   \
        dbg_error("cond %s check fail", #cond); \
    }   \
}while(0);  \
/* pfm_ensure_dbg end */
// 检查cond，不成立则返回ret
#define pfm_ensure_ret(cond, ret)   do{ \
    if(!(cond)) {   \
        dbg_error("cond %s check fail", #cond); \
        return ret; \
    }   \
}while(0);  \
/* pfm_ensure_ret end */
// 检查cond，不成立则返回，用于void类型
#define pfm_ensure_ret_void(cond)   do{ \
    if(!(cond)) {   \
        dbg_error("cond %s check fail", #cond); \
        return; \
    }   \
}while(0);  \
/* pfm_ensure_ret_void end */
// 检查cond，不成立则continue，用在循环中
#define pfm_ensure_continue(cond)   \
{ \
    if(!(cond)) {   \
        dbg_error("cond %s check fail", #cond); \
        continue;   \
    }   \
}   \
/* pfm_ensure_continue end */
// 检查cond，不成立则break，用在循环中
#define pfm_ensure_break(cond)  \
{ \
    if(!(cond)) {   \
        dbg_error("cond %s check fail", #cond); \
        break;  \
    }   \
}   \
/* pfm_ensure_break end */
// 检查cond，不成立则跳转到done
#define pfm_ensure_done(cond)   do{ \
    if(!(cond)) {   \
        dbg_error("cond %s check fail", #cond); \
        goto done;  \
    }   \
}while(0);  \
/* pfm_ensure_done end */
// 检查cond，不成立则手动修改返回值，跳转到done
#define pfm_ensure_ret_val_done(cond, ret, val) do{ \
    if(!(cond)) {   \
        dbg_error("cond %s check fail", #cond); \
        (ret) = (val);  \
        goto done;  \
    }   \
}while(0);  \
/* pfm_ensure_ret_val_done end */

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