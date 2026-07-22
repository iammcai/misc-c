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
 *   1.1 | 2026-06-15 | cai | Support args pass.
 *   2.0 | 2026-07-15 | cai | Use GUN readline.
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
#define cli_register(_cmd, _help, _param, _hook)   \
    _cli_register(_cmd, _help, _param, _param ? array_size(_param) : 0, _hook);  \
/* cli_register end */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       register cli info
 * 
 * @param[in]   cmd     - cli command
 * @param[in]   help    - cli help string
 * @param[in]   param   - param array
 * @param[in]   param_size  - param array size
 * @param[in]   hook    - cli hook func
 * 
 */
static attr_force_inline void _cli_register(const char *cmd, const char *help, 
    cli_param_t *param, unsigned char param_size,  cli_hook_func hook)
{
    _cli_trie_insert(cmd, help, param, param_size, hook);
}

/**
 * @brief       prase string to u32, for cli param
 * 
 * @param[in]   str     - string
 * 
 * @retval      prase result, -1 means fail
 */
extern int cli_param_parse_str_2_u32(const char *str);

/**
 * @brief       cli module init
 * 
 * @note        启动cli服务
 */
extern void cli_module_init();

#endif
/* __CLI_H__ end */