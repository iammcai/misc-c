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
 *   1.1 | 2026-06-15 | cai | Support args pass.
 *   1.2 | 2026-07-15 | cai | Support gnurl Tab.
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

// CLI打印格式
#define CLI_CMD_HELP_FMT    "%-32s%-64s\n"      // "cmd" + "help"
#define CLI_PARAM_FMT       "%-32s  %-62s\n"    // " " + "param"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// cli解析信息定义
typedef struct{
    cli_hook_func hook;     // 回调函数
    cli_param_t *params;    // 参数数组
    unsigned char params_count; // 参数数量

    char **remain_argv;         // 剩余参数列表
    unsigned char remain_argc;  // 剩余参数个数
}cli_match_info_t;

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
        mp_free(item, sizeof(char)*(strlen(item)+1));
        item = array[++idx];
    }
    mp_free(array, sizeof(char*)*(CLI_ITEM_COUNT_MAX + 1));
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

/**
 * @brief       match cmd in trie tree, exclude args
 * 
 * @param[in]   items   - items array, after split
 * @param[in]   items_count - counts of items
 * 
 * @retval      match info
 */
static cli_match_info_t _cli_trie_match(char **items, unsigned char items_count);

/**
 * @brief       parse args and execute cli
 * 
 * @param[in]   match   - 通过 _cli_trie_match 解析得到的信息
 * 
 * @note        解析参数，并执行回调
 */
static void* _cli_trie_parse_execute(cli_match_info_t *match);

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

int cli_param_parse_str_2_u32(const char *str)
{
    int result = 0;

    const char *c = str;
    while(*c != '\0')
    {
        if(!(*c >= '0' && *c <= '9'))
            return -1;
        result = result * 10 + (*c - '0');
        c ++;
    }

    return result;
}

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
        mp_free(array, sizeof(char*)*(CLI_ITEM_COUNT_MAX + 1));
        return NULL;
    }

    return array;
}

void _cli_trie_insert(const char *cmd, const char *help,
    cli_param_t *param, unsigned char param_size, cli_hook_func hook)
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
                item_new->name = mp_strdup(token);
                // trie_item->next扩容，并指定
                trie_item->next = mp_realloc(
                    trie_item->next, (trie_item->size + 1) * sizeof(cli_trie_item_t*), trie_item->size * sizeof(cli_trie_item_t*)
                );
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
            mp_free(trie_item->help, sizeof(char)*(strlen(trie_item->help)+1));     // 支持修改Help
        trie_item->help = mp_strdup(help);
        trie_item->hook = hook;
        trie_item->is_end = 1;
        if(param && param_size)     // 存参数，部分需要深拷贝
        {
            trie_item->params_cnt = param_size;
            trie_item->params = mp_calloc(param_size, sizeof(cli_param_t));
            unsigned char param_i = 0;
            for(param_i = 0; param_i < param_size; ++ param_i)
            {
                trie_item->params[param_i].help = mp_strdup(param[param_i].help);
                trie_item->params[param_i].short_name = param[param_i].short_name;
                trie_item->params[param_i].type = param[param_i].type;
                trie_item->params[param_i].required = param[param_i].required;
            }
        }
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
        strncat(cmd, item->name, cmd_max_len - strlen(cmd));
        // 如果是is_end节点，进行打印
        if(item->is_end)
        {
            safe_printf(CLI_CMD_HELP_FMT, cmd, item->help);
            if(item->params)    // 如果有参数，展示
            {
                unsigned char param_i = 0;
                for(; param_i < item->params_cnt; ++ param_i)
                {
                    cli_param_t *param = &item->params[param_i];
                    char fmt[CLI_INPUT_LEN] = {};
                    if(PARAM_POS == param->type)
                        snprintf(fmt + strlen(fmt), sizeof(fmt) - strlen(fmt), "<value> ");
                    else
                    {
                        if(param->short_name)
                            snprintf(fmt + strlen(fmt), sizeof(fmt) - strlen(fmt), "-%c ", param->short_name);
                        if(PARAM_VALUE == param->type)
                            snprintf(fmt + strlen(fmt), sizeof(fmt) - strlen(fmt), "<value> ");
                    }
                    snprintf(fmt + strlen(fmt), sizeof(fmt) - strlen(fmt), ", %s ", param->required ? "required" : "optional");
                    snprintf(fmt + strlen(fmt), sizeof(fmt) - strlen(fmt), ", help: %s", param->help);
                    safe_printf(CLI_PARAM_FMT, " ", fmt);
                }
            }
        }

        strncat(cmd, " ", cmd_max_len - 1);
    }

    // 递归next
    unsigned char i = 0;
    for(; i < item->size; ++ i)
        _cli_trie_dump_recursive(item->next[i], cmd, cmd_max_len);

    // 复原cmd
    strncpy(cmd, cmd_ori, cmd_max_len);
    mp_free(cmd_ori, cmd_max_len*sizeof(char));
}

void _cli_trie_dump()
{
    cli_trie_item_t *item = &g_cli_trie.root;
    char cmd[CLI_INPUT_LEN] = {};

    ev_with_mutex(&g_cli_trie_mtx)
        _cli_trie_dump_recursive(item, cmd, CLI_INPUT_LEN);
}

cli_match_info_t _cli_trie_match(char **items, unsigned char items_count)
{
    cli_match_info_t match = {};
    cli_trie_item_t *trie_item = &g_cli_trie.root;
    unsigned char i = 0;
    unsigned char remain = items_count;
    cli_trie_item_t *last_end_item = NULL;

    ev_with_mutex(&g_cli_trie_mtx)
    {
        for(i = 0; i < items_count && items[i]; ++ i) // 遍历所有items
        {
            cli_trie_item_t *prefix_match_item = NULL;      // 前缀匹配节点
            unsigned char prefix_match_cnt = 0;

            // 扫描next数组，匹配word
            unsigned char j = 0;
            for(j = 0; j < trie_item->size; ++ j)
                if(!strcmp(items[i], trie_item->next[j]->name))
                    break;
                else if(!strncmp(items[i], trie_item->next[j]->name, strlen(items[i])))     // 尝试前缀匹配
                {
                    prefix_match_cnt += 1;
                    prefix_match_item = trie_item->next[j];
                }

            if(j == trie_item->size && 0 == prefix_match_cnt)       // 查找失败
                break;
            else if(j == trie_item->size && prefix_match_cnt > 1)   // 存在多个前缀匹配
                break;
            else if(j == trie_item->size)
                trie_item = prefix_match_item;
            else
                trie_item = trie_item->next[j];

            remain -= 1;

            // 记录最后一个is_end节点
            if(trie_item->is_end)
                last_end_item = trie_item;
        }
    }

    if(last_end_item)
    {
        match.hook = last_end_item->hook;
        match.params = last_end_item->params;
        match.params_count = last_end_item->params_cnt;
        match.remain_argc = remain;
        match.remain_argv = &items[items_count - remain];   // 指向items的剩余位置
    }

    return match;
}

/**
 * @brief       get ptr to trie item by prefix
 * 
 * @param[in]   prefix      - cmd line
 * 
 * @retval      ptr to item
 * 
 * @note        根据命令前缀，找到对应的trie item，支持前缀匹配
 */
static cli_trie_item_t* _cli_trie_item_get(const char *prefix)
{
    // 没有前缀，返回root
    if(!prefix)
        return &g_cli_trie.root;

    // 将prefix转换为标准的格式，一个text数组
    char** items = _cmd_split(prefix);
    if(!items)
        return NULL;
    unsigned char items_cnt = 0;
    while(items[items_cnt])
        ++ items_cnt;           // 获取items个数

    unsigned char i = 0;
    cli_trie_item_t *cur_item = &g_cli_trie.root;       // 遍历指针，从root开始
    ev_with_mutex(&g_cli_trie_mtx)
    {
        for(; i < items_cnt; ++ i)      // 遍历所有items
        {
            cli_trie_item_t *prefix_match_item = NULL;      // 可前缀匹配上的节点
            unsigned char prefix_match_item_cnt = 0;        // 可前缀匹配上的节点数量

            unsigned char j = 0;
            for(; j < cur_item->size; ++ j)     // 遍历next数组查找
                if(!strcmp(items[i], cur_item->next[j]->name))  // 完全匹配成功
                    break;
                else if(!strncmp(items[i], cur_item->next[j]->name, strlen(items[i])))  // 尝试前缀匹配
                {
                    ++ prefix_match_item_cnt;
                    prefix_match_item = cur_item->next[j];
                }

            if((j == cur_item->size && !prefix_match_item_cnt) ||       // 查找失败
                (j == cur_item->size && prefix_match_item_cnt > 1))     // 存在多个可前缀匹配
            {
                cur_item = NULL;
                goto done;
            }
            else if(j == cur_item->size)        // 唯一前缀匹配，从这个节点继续查找
                cur_item = prefix_match_item;
            else
                cur_item = cur_item->next[j];   // 完全匹配，继续查找
        }
    }
    // 到这里说明找到了，返回cur_item

done:
    _cmd_split_cleanup(items);  // 清理资源
    return cur_item;
}

void* _cli_trie_parse_execute(cli_match_info_t *match)
{
    if(!match || !match->hook)
        return NULL;

    if(!match->remain_argc && !match->params)   // 没有参数，立即执行
    {
        return match->hook(0, NULL);
    }

    dbg("params_count %d, remain_argc %d", match->params_count, match->remain_argc);

    void *ret = NULL;
    // 传给用户的agrv[]必须按照注册顺序
    cli_param_t *usr_param = match->params;
    unsigned char usr_param_cnt = match->params_count;
    unsigned char real_param_cnt = 0;
    char **usr_argv = (char**)mp_calloc(usr_param_cnt, sizeof(char*));      // 给用户的argv
    unsigned char *is_fill = (unsigned char*)mp_calloc(usr_param_cnt, sizeof(unsigned char));   // 是否填充

    unsigned char i = 0;
    for(; i < match->remain_argc;)     // 遍历剩余参数列表，填充到usr_argv
    {
        char *cur_arg = match->remain_argv[i];
        dbg("cur arg: %s", cur_arg);

        // 匹配-标识的短名
        if(strlen(cur_arg) == 2 && cur_arg[0] == '-')
        {
            char *short_name = cur_arg+1;
            unsigned char j = 0;
            for(; j < usr_param_cnt; ++ j)    // 遍历找到long_name匹配
                if(*short_name == match->params[j].short_name && is_fill[j] == 0)
                    break;
            if(j == usr_param_cnt)    // 匹配失败，直接返回
            {
                safe_printf("-cli: %s: Error args\n", cur_arg);
                goto cleanup;
            }
            // 匹配成功，根据是否带值，匹配掉下一个
            if(PARAM_VALUE == match->params[j].type)
            {
                if(i+1 >= match->remain_argc)
                {
                    safe_printf("-cli: %s: Leak value\n", cur_arg);
                    goto cleanup;
                }
                usr_argv[j] = match->remain_argv[i+1];
                i += 2;
            }
            else    // 否则填充一个"1"
            {
                usr_argv[j] = "1";
                i+=1;
            }
            is_fill[j] = 1;     // 标记已填充
            ++ real_param_cnt;
            continue;
        }
        else    // 不带标识的参数，直接填充到下一个
        {
            unsigned char j = 0;
            for(; j < usr_param_cnt; ++ j)
                if(usr_param->type == PARAM_POS && is_fill[j] == 0)
                    break;
            if(j == usr_param_cnt)  // 填充失败
            {
                safe_printf("-cli: %s: Error args\n", cur_arg);
                goto cleanup;
            }
            // 继续
            usr_argv[j] = cur_arg;
            is_fill[j] = 1;
            ++ real_param_cnt;
            i += 1;
        }
    }

    // 检查是否所有必选参数都填充
    for(i = 0; i < usr_param_cnt; ++ i)
    {
        if(usr_param[i].required && !is_fill[i])
        {
            safe_printf("-cli: : Param leakage\n");
            goto cleanup;
        }
    }

    dbg("pass param cnt %d", real_param_cnt);

    ret =  match->hook(real_param_cnt, usr_argv);

cleanup:
    // 释放内存
    mp_free(usr_argv, usr_param_cnt*sizeof(char*));
    mp_free(is_fill, usr_param_cnt*sizeof(unsigned char));
    return ret;
}

void* _cli_trie_excute(const char *cmd)
{
    if(!cmd)
        return NULL;

    // 将cmd转换为标准的格式
    char** items = _cmd_split(cmd);
    if(!items)
        return NULL;

    void *ret = NULL;
    unsigned char items_cnt = 0;
    while(items[items_cnt])
        ++ items_cnt;           // 获取items个数

    // 从前缀树中解析命令，不含参数
    cli_match_info_t match = _cli_trie_match(items, items_cnt);
    if(!match.hook)
        safe_printf("-cli: %s: Unknown command\n", cmd);
    else    // 继续解析参数，并执行
        ret = _cli_trie_parse_execute(&match);

    _cmd_split_cleanup(items);
    return ret;
}

char** cli_trie_completion(const char *prefix, int start, int end, const char *text)
{
    char **result = NULL;
    cli_trie_item_t *item = NULL;

    //dbg_always("prefix: %s, start %d, end %d", prefix, start, end);

    // 判断是否第一个单词，前面有没有空格
    int first_text = 1;
    int j = 0;
    for(; j < start; ++ j)
        if(prefix[j] != ' ')
        {
            first_text = 0;
            break;
        }
    
    // 啥也不输入就tab，直接返回root下挂的一层节点
    if(first_text && start == end)
    {
        item = &g_cli_trie.root;
        result = calloc(item->size+2, sizeof(char*));
        result[0] = strdup("");
        int k = 0;
        for(; k < item->size; ++ k)
            result[k+1] = strdup(item->next[k]->name);
        return result;
    }

    if(first_text)  // tab第一个单词，直接从root开始
        item = &g_cli_trie.root;
    else        // tab之后的单词，需要截断掉最后一个
    {
        char *key = mp_calloc(start, sizeof(char));
        memcpy(key, prefix, start);     // start前面的内存作为查找键
        item = _cli_trie_item_get(key);
        mp_free(key, sizeof(char)*start);
    }

    // tab失败
    if(item == NULL || item->size == 0)
        return NULL;

    result = (char**)calloc(item->size+2, sizeof(char*));   // [0]位置为LCP，也就是text，最后一个为NULL
    result[0] = strdup(text);
    int i = 0;
    int result_size = 1;    // 从1开始填充
    for(; i < item->size; ++ i)
        // 遍历Next，使用前缀匹配
        if(!strncmp(text, item->next[i]->name, strlen(text)))
            result[result_size ++] = strdup(item->next[i]->name);

    if(result_size == 2)    // 如果只有一个匹配项，[0]位置需要和1一致才可以tab补全
    {
        free(result[0]);
        result[0] = strdup(result[1]);
    }

    return result;
}