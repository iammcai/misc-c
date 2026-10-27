/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    cli_pager.h
 * @brief   cli分页器头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-10-27
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-10-27 | cai | Initial creation.
 */

#ifndef __CLI_PAGER_H__
#define __CLI_PAGER_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stddef.h>


#include "plat/compiler.h"
#include "plat/debug.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 每页最大行数
#define CLI_PAGE_LINES_MAX      (22)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// cli状态码
typedef enum{
    CLI_OK = 0,
    CLI_ERR_ABORT,      // 输出中断
    CLI_ERR_NO_MEM,     // 内存分配失败
}cli_status_e;

// cli上下文定义
struct cli_ctx_s{
    size_t current_lines;   // 当前已经打印的行数
    size_t page_limit;      // 每页最大行数
    char is_aborted;        // 是否按下q终止
    void *data;             // 用户数据
};

// cli上下文声明
typedef struct cli_ctx_s cli_ctx_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       int cli ctx
 * 
 * @param[in]   ctx
 */
extern void cli_ctx_init(cli_ctx_t *ctx);

/**
 * @brief       cleanup cli ctx
 * 
 * @param[in]   ctx
 */
extern void cli_ctx_cleanup(cli_ctx_t *ctx);

/**
 * @brief       cli printf
 * 
 * @param[in]   ctx
 * @param[in]   fmt
 * @param[in]   ...
 * 
 * @retval      printf length
 * 
 * @note        cli打印函数，提供给回调使用
 */
extern int cli_printf(cli_ctx_t *ctx, const char *fmt, ...);

#endif