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
 *   1.1 | 2026-06-15 | cai | Support args pass.
 *   1.2 | 2026-07-15 | cai | Support gnurl Tab.
 */

#ifndef __CLI_TRIE_H__
#define __CLI_TRIE_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "plat/compiler.h"
#include "plat/debug.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// cli回调钩子函数签名定义
typedef void* (*cli_hook_func)(unsigned char argc, char *argv[]);

// 节点类型定义
typedef enum{
    PARAM_POS = 0,      // 直接参数，比如 ping <192.168.0.1> 后者
    PARAM_BOOL,         // 不带值的参数，比如 -i
    PARAM_VALUE,        // 带值的参数，比如 -i 1
}cli_param_type_e;

// cli参数定义
typedef struct{
    char short_name;        // 短名参数，例如 -t -i
    cli_param_type_e type;  // 参数类型
    unsigned char required; // 是否必选
    char *help;             // 帮助信息
}cli_param_t;

// cli前缀树节点定义
typedef struct cli_trie_item_s{
    char *name;                         // cmd中的一个单词，需要动态申请内存
    struct cli_trie_item_s **next;      // next array
    unsigned char size;                 // next array size
    char is_end;                        // 是否为cli的结束节点
    cli_hook_func hook;                 // cli回调，is_end == 1 有效
    cli_param_t *params;                // 参数列表，同上
    unsigned char params_cnt;           // 参数个数
    char *help;                         // help信息，同上
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
 * @param[in]   param   - param array
 * @param[in]   param_size  - param array size
 * @param[in]   hook    - hook func
 * 
 * @note        内部使用，命令加入前缀树
 */
extern void _cli_trie_insert(const char *cmd, const char *help,
    cli_param_t *array, unsigned char param_size, cli_hook_func hook);

/**
 * @brief       excute cmd by trie
 * 
 * @retval      if cmd valid, return hook ret
 * 
 * @note        内部使用，根据cmd，在trie中查找，解析，执行
 */
extern void* _cli_trie_excute(const char *cmd);

/**
 * @brief       dump cli trie
 * 
 * @note        打印cli前缀树
 */
extern void _cli_trie_dump();

/**
 * @brief       search in trie by prefix，find possibly text for completion
 * 
 * @param[in]   prefix  - prefix
 * @param[in]   start   - last text start index
 * @param[in]   end     - last text end index
 * @param[in]   text    - last text
 * 
 * @retval      possilbly text array, end by NULL
 * 
 * @note        例如输入show sys，那么prefix == show sys, start == 5, end == 7
 *              找到show节点下sys可以补全的单词数组
 */
extern char** cli_trie_completion(const char *prefix, int start, int end, const char *text);

#endif
/* __CLI_TRIE_H__ end */