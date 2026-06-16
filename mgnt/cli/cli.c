/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    cli.c
 * @brief   CLI实现
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
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <pthread.h>
#include "cli/cli.h"
#include "event/ev_lock.h"
#include "mp/mp.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 初始预设CLI条目数量
#define CLI_COUNTS_INIT     (128)

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       handle cli cmd
 * 
 * @param[in]   cmd - command
 * 
 * @note        处理cli命令，调用相应回调
 */
static attr_force_inline void _cli_handler(const char *cmd)
{
    _cli_trie_excute(cmd);
}

/**
 * @brief       cli routine, wait user input and run cli
 * 
 * @note        监听用户输入的线程入口
 */
static void* _cli_routine(void *args);

/**
 * @brief       hook func of cli cmd "?"
 * 
 * @note        ? 的回调函数
 */
static attr_force_inline void* _cli_cmd_dump(unsigned char argc, char *argv[])
{
    _cli_trie_dump();
    return NULL;
}

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

// 执行监听输入、任务处理的线程
static pthread_t cli_stask_tid;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static void* _cli_routine(void *args)
{
    dbg_major("CLI start...");
    while(1)
    {
        char input[CLI_INPUT_LEN] = {};

        safe_printf(fmt_color_green "misc-c > " fmt_color_clear);

        if(fgets(input, CLI_INPUT_LEN, stdin));
        {
            // 去除末尾的\n，回车导致的
            if(strlen(input))
                input[strlen(input)-1] = '\0';

            if(strlen(input))       // 空内容不处理
                _cli_handler(input);
        }
    }

    return NULL;
}

void cli_init()
{
    // cli注册
    cli_register("?", "dump all cli cmd", NULL, _cli_cmd_dump);
    cli_register("help", "dump all cli cmd", NULL, _cli_cmd_dump);

    // 创建线程执行监听cli输入
    pthread_create(&cli_stask_tid, NULL, _cli_routine, NULL);
}