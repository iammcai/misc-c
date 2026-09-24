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
 *   3.0 | 2026-09-18 | cai | Optimize cli register method.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <sys/prctl.h>
#include <signal.h>

#include "cli/cli.h"
#include "cli/cli_gnurl.h"
#include "event/ev_lock.h"
#include "mp/mp.h"
#include "event/ev_sem.h"

// include for cli register
#include "led/led.h"
#include "sht30/sht30.h"
#include "sysmon/sysmon.h"
#include "zcap/zcap.h"
#include "ftx/ftx.h"
#include "nt/arp.h"
#include "nt/ping.h"
#include "nt/tftp.h"
#include "cmcp/cmcp.h"
#include "mp/mp_fixed.h"
#include "mp/mp_nonfixed.h"
#include "mp/mp_slab.h"
#include "thp/thp.h"
#include "syslog/syslog.h"
#include "swdg/swdg.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 初始预设CLI条目数量
#define CLI_COUNTS_INIT     (128)

// 生成注册表项
#define CLI_REG_ENTRY(c, he, p, ho)     {c, he, p, ho}

// 带参数生成CLI注册表项
#define CLI_REG_ENTRY_WITH_PARAM(c, he, param_name, ho) \
    {c, he, cli_param_reg_ ## param_name.param, cli_param_reg_ ## param_name.cnt, ho}

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// cli模式枚举
typedef enum{
    CLI_MODE_CONFIG = 0,
    CLI_MODE_SHELL,
}cli_mode_e;

// cli注册表项定义
typedef struct{
    const char* cmd;        // cli命令
    const char* help;       // helper string
    cli_param_t *param;     // 参数
    cli_hook_func hook;     // 回调函数
}cli_register_entry_t;

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
static attr_force_inline void _cli_cmd_dump(unsigned char argc, char *argv[])  // cppcheck-suppress constParameterCallback
{
    _cli_trie_dump();
}

/**
 * @brief       enter cli shell mode
 * 
 * @note        输入直接传给linux shell，输入config退出
 */
static attr_force_inline void _cli_shell_cli_hook(unsigned char argc, char *argv[]);   // cppcheck-suppress constParameterCallback

/**
 * @brief       cli hook for history
 */
static void _cli_dump_history_cli_hook(unsigned char argc, char *argv[]);

/**
 * @brief       进入CLI后打印提示语
 */
static attr_force_inline void _cli_hello_printf()
{
    safe_printf("\n");
    safe_printf(fmt_color_cyan);
    safe_printf("  __  __ _      ____       ___ \n");
    safe_printf(" |  \\/  (_)___ / ___|     / __|\n");
    safe_printf(" | |\\/| | / __| |    _____| |  \n");
    safe_printf(" | |  | | \\__ \\ |___|_____| |__\n");
    safe_printf(" |_|  |_|_|___/\\____|     \\___|\n");
    safe_printf(fmt_color_clear);
    safe_printf("\n");
    safe_printf(" System initialized successfully.\n");
    safe_printf(" Version: %s | Author: Cai\n", "1.0");
    safe_printf(" Enter '?' or 'help' to list all commands.\n");
    safe_printf("\n");
}

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

extern ATOMIC_UINT8_T core_running;

// 执行监听输入、任务处理的线程
static pthread_t cli_task_tid;
// 线程运行标志
static ATOMIC_UINT8_T cli_task_running = 0;

// cli模式
cli_mode_e g_cli_mode = CLI_MODE_CONFIG;

extern ev_sem_t g_platform_init_ok;

// cli history param
static cli_param_t param0[] = {
    {.help = "history num", .required = 0, .short_name = 'n', .type = PARAM_VALUE},
    {},
};

// TODO: 全局cli注册表
const static cli_register_entry_t g_cli_register_table[] = {
    // cli module
    CLI_REG_ENTRY("?", "dump all cli cmd", NULL, _cli_cmd_dump),
    CLI_REG_ENTRY("help", "dump all cli cmd", NULL, _cli_cmd_dump),
    CLI_REG_ENTRY("shell", "enter cli shell mode", NULL, _cli_shell_cli_hook),
    CLI_REG_ENTRY("history", "dump cli history", param0, _cli_dump_history_cli_hook),
    // led
    CLI_REG_ENTRY("led on", "turn on led", NULL, led_on_cli_hook),
    CLI_REG_ENTRY("led off", "turn off led", NULL, led_off_cli_hook),
    // sht30
    CLI_REG_ENTRY("show temperature", "dump temperature measured by sht30", NULL, sht30_show_temp),
    CLI_REG_ENTRY("show humidity", "dump humidity measured by sht30", NULL, sht30_show_humi),
    // sysmon
    CLI_REG_ENTRY("show cpu usage", "dump cpu usage", NULL, sysmon_show_cpu_usage_cli_hook),
    // zcap
    CLI_REG_ENTRY("show zcaptor", "dump info of zcaptor", NULL, zcap_dump_captor_cli_hook),
    CLI_REG_ENTRY("zcaptor dump packet", "dump info of rx packet during a preiod", zcap_dump_pkt_param, zcap_dump_pkt_cli_hook),
    // ftx
    CLI_REG_ENTRY("show ftxor", "dump all ftxor", NULL, ftxor_dump_cli_hook),
    // arp
    CLI_REG_ENTRY("arp probe", "send arp probe", arp_probe_param, arp_probe_cli_hook),
    CLI_REG_ENTRY("arp scan", "arp scan", arp_scan_param, arp_scan_cli_hook),
    CLI_REG_ENTRY("show arp cache", "dump arp cache", NULL, arp_show_cache_cli_hook),
    // ping
    CLI_REG_ENTRY("ping", "ping", ping_param, ping_cli_hook),
    // tftp
    CLI_REG_ENTRY("tftp download", "download file from server", tftp_dl_param, tftp_download_cli_hook),
    CLI_REG_ENTRY("tftp upload", "upload file to server", tftp_ul_param, tftp_upload_cli_hook),
    // cmcp
    CLI_REG_ENTRY("cmcp on", "enable cmcp", NULL, cmcp_on_cli_hook),
    CLI_REG_ENTRY("cmcp off", "disable cmcp", NULL, cmcp_off_cli_hook),
    // mp
    CLI_REG_ENTRY("debug show meminfo", "show calloc/free from/to system", NULL, mp_show_mem_usage_cli_hook),
    CLI_REG_ENTRY("debug show mp fixed", "debug dump all fixed mp info", NULL, mp_fixed_debug_show),
    CLI_REG_ENTRY("debug show mp nonfixed", "dump nonfixed mp debug info", NULL, mp_nonfixed_debug_cli_hook),
    CLI_REG_ENTRY("debug show mp slab", "debug dump slab type mempool", NULL, mp_slab_show_cli_hook),
    // TODO: debug
    // thp
    CLI_REG_ENTRY("show thread pool", "show all thread pools info", NULL, thp_dump_cli_hook),
    // syslog
    CLI_REG_ENTRY("show syslog buffer", "dump syslog ring buffer", syslog_get_param, syslog_show_buffer_cli_hook),
    CLI_REG_ENTRY("show syslog direction", "show syslog direction", NULL, syslog_direction_get_cli_hook),
    CLI_REG_ENTRY("syslog direction", "set syslog direction", syslog_dir_param, syslog_direction_cli_hook),
    // swdg
    CLI_REG_ENTRY("show swdg", "show software watch dog status", NULL, swdg_show_swdg_cli_cb),
};

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static inline void _cli_shell_cli_hook(unsigned char argc, char *argv[])    // cppcheck-suppress constParameterCallback
{
    safe_printf("-cli: Shell mode, Enter <config> for coming back.\n");
    g_cli_mode = CLI_MODE_SHELL;
}

static void* _cli_routine(void *args)
{
    prctl(PR_SET_NAME, "cli_main");

    ev_sem_wait(&g_platform_init_ok);

    ATOM_STORE(&cli_task_running, 1, MORDER_RELAXED);

    _cli_hello_printf();        // 打印提示语

    while(ATOM_LOAD(&cli_task_running, MORDER_RELAXED))
    {
        char *input = NULL;

        switch(g_cli_mode)
        {
            case CLI_MODE_CONFIG:
                input = cli_gnurl_readline(fmt_color_green "misc-c (config) > " fmt_color_clear);
                if(!ATOM_LOAD(&core_running, MORDER_ACQUIRE))
                {
                    ATOM_STORE(&cli_task_running, 0, MORDER_RELAXED);
                    break;
                }
                if(!input)      // ctrl + D 退出cli
                {
                    dbg_always("input is NULL");
                    if(ATOM_LOAD(&core_running, MORDER_ACQUIRE))    // 检查core_running标志
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
                if(!ATOM_LOAD(&core_running, MORDER_ACQUIRE))
                {
                    ATOM_STORE(&cli_task_running, 0, MORDER_RELAXED);
                    break;
                }
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

    return NULL;
}

static void _cli_dump_history_cli_hook(unsigned char argc, char *argv[])
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
            return;
        }
        num = length < n ? length : n;
    }

    if(num == 0)
        return;

    int start = base + length - num;
    int end = base + length;
    for(; start < end; ++ start)
        safe_printf("%-3d : %s\n", start, cli_gnurl_history_get(start));
}

void cli_module_init()
{
    // 初始化gnureadline
    cli_gnurl_init();

    cli_param_t dummy = {};
    // 导入cli注册表项
    for(int i = 0; i < array_size(g_cli_register_table); ++ i)
    {
        int param_cnt = 0;
        if(g_cli_register_table[i].param)   // 参数列表以{}哨兵表示结尾
        {
            cli_param_t *p_param = NULL;
            while((p_param = &g_cli_register_table[i].param[param_cnt]) && memcmp(p_param, &dummy, sizeof(cli_param_t)))
                ++ param_cnt;
        }
        _cli_register(
            g_cli_register_table[i].cmd,
            g_cli_register_table[i].help,
            g_cli_register_table[i].param,
            param_cnt,
            g_cli_register_table[i].hook
        );
    }

    // 创建线程执行监听cli输入
    pthread_create(&cli_task_tid, NULL, _cli_routine, NULL);

    dbg_major("cli module init ok");
}

void cli_module_exit_th()
{
    pthread_join(cli_task_tid, NULL);

    dbg_major("cli module exit top half ok");
}

void cli_module_exit_bh()
{
    // TODO: 回收资源
    cli_trie_destory();     // 销毁前缀树

    dbg_major("cli module exit bottom half ok");
}