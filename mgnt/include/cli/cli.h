/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    cli.h
 * @brief   CLI头文件
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

#ifndef __CLI_H__
#define __CLI_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "cli/cli_trie.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/**
 * 外部使用，注册cli信息
 */
#define cli_register(_cmd, _help, _hook)   \
    _cli_register(_cmd, _help, _hook);   \
/* cli_register end */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       register cli info
 * 
 * @param[in]   cmd     - cli command
 * @param[in]   help    - cli help string
 * @param[in]   hook    - cli hook func
 * 
 */
static attr_force_inline void _cli_register(const char *cmd, const char *help, cli_hook_func hook)
{
    _cli_trie_insert(cmd, help, hook);
}

/**
 * @brief       cli module init
 * 
 * @note        启动cli服务
 */
extern void cli_init();

#endif
/* __CLI_H__ end */