/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    cli_gnurl.h
 * @brief   GNU readline API封装
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-07-15
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-07-15 | cai | Initial creation.
 */

#ifndef __CLI_GNURL_H__
#define __CLI_GNURL_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <readline/readline.h>
#include <readline/history.h>
#include "plat/compiler.h"

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       set max history count
 * 
 * @param[in]   capacity    - capacity
 * 
 * @note        设置历史记录最大数量，超出会丢弃最旧的条目
 */
extern void cli_gnurl_history_capacity_set(unsigned int capacity);

/**
 * @brief       readline, wait user input
 * 
 * @param[in]   prompt  - prompt
 * 
 * @retval      input string
 * 
 * @note        返回值通过malloc分配内存，需要free
 */
extern char* cli_gnurl_readline(const char *prompt);

/**
 * @brief       add history
 * 
 * @param[in]   input   - input
 * 
 * @note        内含去重
 */
extern void cli_gnurl_add_history(const char *input);

/**
 * @brief       get current length of cli history
 * 
 * @retval      history counts
 */
static attr_force_inline int cli_gnurl_history_length_get()
{
    return history_length;
}

/**
 * @brief       get current base of cli history
 * 
 * @retval      history base index
 * 
 * @note        history索引并不一定从0开始，这里获取真实的base
 */
static attr_force_inline int cli_gnurl_history_base_get()
{
    return history_base;
}

/**
 * @brief       get cli histroy by index
 * 
 * @param[in]   index   - index
 * 
 * @retval      history line
 */
static attr_force_inline const char* cli_gnurl_history_get(int index)
{
    if(index >= history_base + history_length || index < history_base)
        return NULL;
    HIST_ENTRY *history = history_get(index);
    if(!history)
        return NULL;
    return (const char*)history->line;
}

/**
 * @brief       init gun readline
 * 
 * @note        初始化一些GNU readline配置
 */
extern void cli_gnurl_init();

#endif
/* __CLI_GNURL_H__ end */