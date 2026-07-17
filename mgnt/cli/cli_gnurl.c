/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    cli_gnurl.c
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

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "cli/cli_gnurl.h"
#include "cli/cli_trie.h"
#include "plat/debug.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 记录历史cli的最大数量
#define CLI_GNURL_HISTORY_CAPACITY  (512)
// 历史记录的文件路径
#define CLI_GNURL_HISTORY_PATH      "../mgnt/cli/history.txt"

/* ========================================================================== */
/*                              Static Variables                              */
/* ========================================================================== */

// 上一次写入文件的位置
static int s_last_synced_len = 0;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       gnu readline tab callback
 * 
 * @param[in]   text    - text patch
 * @param[in]   start   - text start offset of rl_line_buffer
 * @param[in]   end     - text end offset of rl_line_buffer
 * 
 * @retval      possibly text array, end by NULL
 * 
 * @note        例如输入show sys，text为sys，start为5，end为8
 *              返回值必须是malloc等动态分配的，rl会负责free
 */
static attr_force_inline char **_cli_gnurl_completion(const char *text, int start, int end);

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void cli_gnurl_history_capacity_set(unsigned int capacity)
{
    stifle_history(capacity);
}

char* cli_gnurl_readline(const char *prompt)
{
    char *input = readline(prompt);
    return input;
}

void cli_gnurl_add_history(const char *input)
{
    if(!input)
        return;

    HIST_ENTRY *last = history_get(history_length);     // 获取最近的一条历史
    if(!last || 0 != strcmp(last->line, input))         // 不重复才添加历史
    {
        add_history(input);
        int new_entry = history_length - s_last_synced_len;
        if(new_entry > 0)
        {
            append_history(new_entry, CLI_GNURL_HISTORY_PATH);
            s_last_synced_len = history_length;
        }
    }
        
}

static inline char **_cli_gnurl_completion(const char *text, int start, int end)
{
    rl_attempted_completion_over = 1;           // 禁止使用文件名补全
    return cli_trie_completion(rl_line_buffer, start, end, text);
}

void cli_gnurl_init()
{
    // 设置历史记录最大数量
    cli_gnurl_history_capacity_set(CLI_GNURL_HISTORY_CAPACITY);

    // 从文件读取历史记录
    read_history(CLI_GNURL_HISTORY_PATH);
    s_last_synced_len = history_length;

    rl_completion_append_character = ' ';       // 补全自动加空格
    rl_attempted_completion_function = _cli_gnurl_completion;   // 指定高级补全函数
}