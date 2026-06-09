/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    cli_trie.h
 * @brief   CLI字典树头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-10
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-10 | cai | Initial creation.
 */

#ifndef __CLI_TRIE_H__
#define __CLI_TRIE_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "plat/compiler.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// cli回调钩子函数签名定义
typedef void* (*cli_hook_func)(unsigned char argc, char *argv[]);

// cli前缀树节点定义
typedef struct cli_trie_item_s{
    const char *name;                   // cmd中的一个单词，需要动态申请内存
    struct cli_trie_item_s **next;      // next array
    unsigned char size;                 // next array size
    char is_end;                        // 是否为cli的结束节点
    cli_hook_func hook;                 // cli回调，is_end == 1 有效
    const char *help;                   // help信息，同上
}cli_trie_item_t;

// cli前缀树定义
typedef struct{
    cli_trie_item_t root;   // 根节点，作为哨兵使用
}cli_trie_root_t;

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 用户输入缓冲区长度
#define CLI_INPUT_LEN       (256)

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       insert cli cmd into trie
 * 
 * @param[in]   cmd     - cli cmd
 * @param[in]   help    - cli help string
 * @param[in]   hook    - hook func
 * 
 * @note        内部使用，命令加入前缀树
 */
extern void _cli_trie_insert(const char *cmd, const char *help, cli_hook_func hook);

/**
 * @brief       find hook by cmd
 * 
 * @note        内部使用，根据cmd内容找到hook func
 */
extern cli_hook_func _cli_trie_find(const char *cmd);

/**
 * @brief       dump cli trie
 * 
 * @note        打印cli前缀树
 */
extern void _cli_trie_dump();

#endif
/* __CLI_TRIE_H__ end */