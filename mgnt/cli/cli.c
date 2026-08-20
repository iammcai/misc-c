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
 *   2.0 | 2026-07-15 | cai | Use GUN readline.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include "cli/cli.h"
#include "cli/cli_gnurl.h"
#include "event/ev_lock.h"
#include "mp/mp.h"
#include "event/ev_sem.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 初始预设CLI条目数量
#define CLI_COUNTS_INIT     (128)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// cli模式枚举
typedef enum{
    CLI_MODE_CONFIG = 0,
    CLI_MODE_SHELL,
}cli_mode_e;

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
static pthread_t cli_task_tid;
// 线程运行标志
static ATOMIC_UINT8_T cli_task_running = 0;

// cli模式
cli_mode_e g_cli_mode = CLI_MODE_CONFIG;

extern ev_sem_t g_platform_init_ok;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static attr_force_inline void* _cli_shell_cli_hook(unsigned char argc, char *argv[])
{
    safe_printf("-cli: Shell mode, Enter <config> for coming back.\n");
    g_cli_mode = CLI_MODE_SHELL;
    return NULL;
}

static void* _cli_routine(void *args)
{
    ev_sem_wait(&g_platform_init_ok);

    ATOM_STORE(&cli_task_running, 1, MORDER_RELAXED);

    dbg_major("CLI start...");
    while(ATOM_LOAD(&cli_task_running, MORDER_RELAXED))
    {
        char *input = NULL;

        switch(g_cli_mode)
        {
            case CLI_MODE_CONFIG:
                input = cli_gnurl_readline(fmt_color_green "misc-c (config) > " fmt_color_clear);
                if(!input)      // ctrl + D 退出cli，ctrl + C 结束程序
                {
                    if(errno != EINTR)
                        g_cli_mode = CLI_MODE_SHELL;
                    break;
                }
                if(*input)      // 非空进行处理
                {
                    _cli_handler(input);    // readline会自动过滤掉换行
                    cli_gnurl_add_history(input);       // 添加历史
                }
                // 需要free，readline使用的是malloc分配的内存
                free(input);
                break;
            case CLI_MODE_SHELL:
                input = cli_gnurl_readline(fmt_color_green "misc-c (shell) > " fmt_color_clear);
                if(0 == strcmp(input, "config"))
                    g_cli_mode = CLI_MODE_CONFIG;
                else
                    system(input);
                free(input);
                break;
            default:
                break;
        }
    }

    dbg("cli thread exit");

    return NULL;
}

static void* _cli_dump_history_cli_hook(unsigned char argc, char *argv[])
{
    int length = cli_gnurl_history_length_get();    // histroy数量
    int base = cli_gnurl_history_base_get();        // base index
    int num = length;

    if(argc == 1)
    {
        int n = cli_param_parse_str_2_u32(argv[0]);
        if(n < 0)
        {
            safe_printf("-cli: Error param: %s\n", argv[0]);
            return NULL;
        }
        num = length < n ? length : n;
    }

    if(num == 0)
        return NULL;

    int start = base + length - num;
    int end = base + length;
    for(; start < end; ++ start)
        safe_printf("%-3d : %s\n", start, cli_gnurl_history_get(start));

    return NULL;
}

void cli_module_init()
{
    // 初始化gnureadline
    cli_gnurl_init();

    // cli注册
    cli_register("?", "dump all cli cmd", NULL, _cli_cmd_dump);
    cli_register("help", "dump all cli cmd", NULL, _cli_cmd_dump);
    cli_param_t param0[] = {
        {.help = "history num", .required = 0, .short_name = 'n', .type = PARAM_VALUE},
    };
    cli_register("history", "dump cli history", param0, _cli_dump_history_cli_hook);
    cli_register("shell", "enter cli shell mode", NULL, _cli_shell_cli_hook);

    // 创建线程执行监听cli输入
    pthread_create(&cli_task_tid, NULL, _cli_routine, NULL);

    dbg_major("cli module init ok");
}

void cli_module_deinit_th()
{
    ATOM_STORE(&cli_task_running, 0, MORDER_RELAXED);
    // 向cli线程发送SIGINT，退出gnu_readline的阻塞等待
    pthread_kill(cli_task_tid, SIGINT);

    dbg_major("cli module deinit top half ok");
}