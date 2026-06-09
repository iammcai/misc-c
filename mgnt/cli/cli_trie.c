/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    cli_trie.c
 * @brief   CLI字典树实现
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

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stddef.h>
#include <string.h>
#include "cli/cli_trie.h"
#include "mp/mp.h"
#include "plat/debug.h"
#include "event/ev_lock.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 一行cli中单词最大数量
#define CLI_ITEM_COUNT_MAX      (64)

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       clean up _cmd_split mem
 * 
 * @param[in]   array   - buffer to free
 * 
 * @note        释放_cmd_split接口中申请的动态内存
 */
static attr_force_inline void _cmd_split_cleanup(char **array)
{
    assert(array);
    char *item = array[0];
    unsigned char idx = 0;
    while(item)
    {
        mp_free(item);
        item = array[++idx];
    }
    mp_free(array);
}

/**
 * @brief       split cmd to items
 * 
 * @param[in]   cmd     - cli command
 * 
 * @retval      ptr to char* array, end by NULL
 * 
 * @note        动态分配内存返回，需要搭配_cmd_split_cleanup函数释放
 */
static char** _cmd_split(const char *cmd);

/**
 * @brief       recursive dump cli trie
 * 
 * @param[in]   item    - ptr to current item
 * @param[in]   cmd     - current cmd string
 * @param[in]   cmd_max_len     - max len of cmd buffer
 */
static void _cli_trie_dump_recursive(cli_trie_item_t *item, char *cmd, unsigned short cmd_max_len);

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

// 全局cli前缀树
static cli_trie_root_t g_cli_trie = {};
// 全局互斥锁，保护前缀树
static ev_mutex_t g_cli_trie_mtx = EV_MUTEX_INITIALIZER;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static char** _cmd_split(const char *cmd)
{
    assert(cmd);

    char **array = mp_calloc(CLI_ITEM_COUNT_MAX + 1, sizeof(char*));    // 留一个NULL
    unsigned char size = 0;
    unsigned char left = 0, right = 0;      // 左右指针分割
    unsigned char cmd_len = strlen(cmd);

    while(right < cmd_len)      // 以右边界为界
    {
        while(left < cmd_len && cmd[left] == ' ') // left跳过空格
            ++ left;
        if(left >= cmd_len)     // 检查left是否越界
            break;

        right = left + 1;       // right开始查找
        while(right < cmd_len && cmd[right] != ' ')
            ++ right;

        // 至此，cmd[left, right)为一个item
        unsigned char item_size = right - left;
        char *item = mp_calloc(item_size + 1, sizeof(char));    // +1 留给'\0'
        memcpy(item, cmd+left, item_size);
        array[size++] = item;       // 加入数组

        // left，right更迭
        left = right + 1;
    }

    if(!size)       // 检查是否分割结果为空，纯空格
    {
        mp_free(array);
        return NULL;
    }

    return array;
}

void _cli_trie_insert(const char *cmd, const char *help, cli_hook_func hook)
{
    char **items = NULL;
    char *token = NULL;
    cli_trie_item_t *trie_item = &g_cli_trie.root;

    // cmd首先需要分割为多个item
    items = _cmd_split(cmd);
    if(!items)
        return;

    ev_with_mutex(&g_cli_trie_mtx)
    {
        // 开始加入前缀树
        token = items[0];
        unsigned char index = 0;
        while(token)
        {
            // 查找下一层，也就是本节点next array中有无匹配的
            unsigned char i = 0;
            for(; i < trie_item->size; ++ i)
                if(!strcmp(token, trie_item->next[i]->name))     // 完全匹配
                    break;
            if(i < trie_item->size) // 找到，那么更新trie_item，继续查找
                trie_item = trie_item->next[i];
            else        // 否则执行插入
            {
                // 新增节点
                cli_trie_item_t *item_new = mp_calloc(1, sizeof(cli_trie_item_t));
                item_new->name = strdup(token);
                // trie_item->next扩容，并指定
                trie_item->next = mp_realloc(trie_item->next, (trie_item->size + 1) * sizeof(cli_trie_item_t*));
                trie_item->next[trie_item->size] = item_new;
                trie_item->size ++;
                // 更新trie_item
                trie_item = item_new;
            }
            // 更新token，继续查找
            token = items[++index];
        }

        // 此时trie_item指向的是末尾的节点
        if(trie_item->help)
            mp_free(trie_item->help);   // 支持修改Help
        trie_item->help = strdup(help);
        trie_item->hook = hook;
        trie_item->is_end = 1;
    }

    _cmd_split_cleanup(items);      // 释放内存
}

static void _cli_trie_dump_recursive(cli_trie_item_t *item, char *cmd, unsigned short cmd_max_len)
{
    if(!item)
        return;

    // 暂存原来的cmd，添加该item->name
    char *cmd_ori = mp_calloc(cmd_max_len, sizeof(char));
    memcpy(cmd_ori, cmd, cmd_max_len);

    if(item->name)      // 排除掉root节点
    {
        strncat(cmd, item->name, strlen(item->name));
        // 如果是is_end节点，进行打印
        if(item->is_end)
            printf("[cmd]: %s\n\t[help]: %s\n", cmd, item->help);
        strcat(cmd, " ");
    }

    // 递归next
    unsigned char i = 0;
    for(; i < item->size; ++ i)
        _cli_trie_dump_recursive(item->next[i], cmd, cmd_max_len);

    // 复原cmd
    strncpy(cmd, cmd_ori, cmd_max_len);
    mp_free(cmd_ori);
}

void _cli_trie_dump()
{
    cli_trie_item_t *item = &g_cli_trie.root;
    char cmd[CLI_INPUT_LEN] = {};

    ev_with_mutex(&g_cli_trie_mtx)
    {
        _cli_trie_dump_recursive(item, cmd, CLI_INPUT_LEN);
    }
}

cli_hook_func _cli_trie_find(const char *cmd)
{
    if(!cmd)
        return NULL;

    char** items = _cmd_split(cmd);
    if(!items)
        return NULL;

    unsigned char index = 0;
    char *token = items[0];
    cli_trie_item_t *trie_item = &g_cli_trie.root;
    ev_with_mutex(&g_cli_trie_mtx)
    {
        while(token)
        {
            unsigned char idx = 0;
            for(; idx < trie_item->size; ++ idx)
                if(!strcmp(token, trie_item->next[idx]->name))
                    break;
            // 查找失败，直接返回
            if(idx == trie_item->size)
                return NULL;
            // 否则继续查找
            token = items[++index];
            trie_item = trie_item->next[idx];
        }
    }
    // 此时trie_item就是目标，检查is_end
    return trie_item->is_end ? trie_item->hook : NULL;
}