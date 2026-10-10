/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    core.c
 * @brief   核心进程
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-09-18
 * @version 1.0
 *
 * @note    misc-c主进程
 *
 * @history
 *   1.0 | 2026-09-18 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <sys/types.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <signal.h>

#include "plat/compiler.h"
#include "plat/debug.h"
#include "proc/proc.h"
#include "proc/heart_beat.h"

// include for init
#include "swdg/swdg.h"
#include "cli/cli.h"
#include "event/ev_thread.h"
#include "event/ev_loop.h"
#include "mp/mp.h"
#include "mp/mp_fixed.h"
#include "mp/mp_nonfixed.h"
#include "mp/mp_slab.h"
#include "thp/thp.h"
#include "syslog/syslog.h"
#include "zcap/zcap.h"
#include "ftx/ftx.h"
#include "nt/arp.h"
#include "nt/ping.h"
#include "nt/tftp.h"
#include "cmcp/cmcp.h"
#include "web/web.h"
#include "led/led.h"
#include "sht30/sht30.h"
#include "ssd1306/ssd1306.h"
#include "sysmon/sysmon.h"
#include "test_proc/test.h"
#include "rpc/rpc.h"
#include "ipc/shm/shm.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 模块初始化注册项生成
#define MODULE_INIT_ENTRY(n, f)     {#n, f}
// 模块退出注册项生成
#define MODULE_EXIT_ENTRY(n, f)     {#n, f}
// 进程入口表项生成
#define PROCESS_ENTRY(i, n, f)      {i, #n, f}

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 模块初始化函数签名
typedef void (*module_init_f)(void);
// 模块退出函数签名
typedef void (*module_exit_f)(void);
// 进程入口函数签名
typedef void (*process_entry_f)(void);

// 模块初始化表项定义
typedef struct{
    const char *name;
    module_init_f func;
}module_init_entry_t;

// 模块退出表项定义
typedef struct{
    const char *name;
    module_exit_f func;
}module_exit_entry_t;

// 进程入口表项定义
typedef struct{
    proc_id_e pid;
    const char *name;
    process_entry_f func;
}process_entry_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       init platform
 * 
 * @note        平台初始化
 */
static void platform_init();

/**
 * @brief       core进程内部一些独立出来的需要提前进行的初始化，比如先启动接口抓包
 */
static void core_iso_init();

/**
 * @brief       core iso exit
 */
static void core_iso_exit();

/**
 * @brief       init signal handle
 * 
 * @note        注册信号处理，fork前调用，子进程继承
 */
static void signal_init();

/**
 * @brief       signal handler
 * 
 * @note        处理信号，core进行处理
 */
static attr_force_inline void signal_handler(int signo);

/**
 * @brief       exit other process
 */
static void _proc_exit();

/**
 * @brief       rpc printf hello
 */
static void _rpc_printf_hello(rpc_header_t *hdr, rpc_in_param_t *in_param)
{
    dbg_always("hello");
}

/* ========================================================================== */
/*                             Extern Symbols                                 */
/* ========================================================================== */

// core运行标志
ATOMIC_UINT8_T core_running;

// 用于通知platform init ok
ev_sem_t g_platform_init_ok;

declare_zcap(eth0)
declare_zcap(wlan0)

// 非core运行标志
static shm_mgnt_t *rpc_process_running;

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 公共模块初始化top half集合。fork前执行，对父子进程都有效，要求不允许模块创建任何线程
static const module_init_entry_t module_init_th_common_table[] = {
    MODULE_INIT_ENTRY(swdg_init_th,         swdg_module_init_th),
    MODULE_INIT_ENTRY(ev_thd_init,          ev_thd_module_init),
    MODULE_INIT_ENTRY(el_init_th,           ev_loop_module_init_th),
    MODULE_INIT_ENTRY(mta_init,             mem_type_attr_module_init),
    MODULE_INIT_ENTRY(mp_fixed_init_th,     mp_fixed_module_init_th),
    MODULE_INIT_ENTRY(mp_nonfixed_init_th,  mp_nonfixed_module_init_th),
    MODULE_INIT_ENTRY(thread_pool_init,     thp_module_init),
};

// 公共模块初始化bottom half集合。fork后父子进程都执行，可创建任何线程
static const module_init_entry_t module_init_bh_common_table[] = {
    MODULE_INIT_ENTRY(timer_init,           ev_timer_module_init),
    MODULE_INIT_ENTRY(el_init_bh,           ev_loop_module_init_bh),
    MODULE_INIT_ENTRY(mp_fixed_init_bh,     mp_fixed_module_init_bh),
    MODULE_INIT_ENTRY(slab_mp_init,         mp_slab_module_init),
    MODULE_INIT_ENTRY(mp_nonfixed_init_bh,  mp_nonfixed_module_init_bh),
    MODULE_INIT_ENTRY(swdg_init_bh,         swdg_module_init_bh),
    MODULE_INIT_ENTRY(proc_hb_init,         proc_heart_beat_module_init),
    MODULE_INIT_ENTRY(proc_rpc_init,        rpc_module_init),
};

// 公共模块退出top half函数集合，在进程自身退出函数前调用
static const module_exit_entry_t module_exit_th_common_table[] = {
    MODULE_EXIT_ENTRY(proc_rpc_exit,        rpc_module_exit),
    MODULE_EXIT_ENTRY(proc_hb_exit,         proc_heart_beat_module_exit),
    MODULE_EXIT_ENTRY(swdg_exit_th,         swdg_module_exit_th),
    MODULE_EXIT_ENTRY(mp_nonfxied_exit_th,  mp_nonfixed_module_exit_th),
    MODULE_EXIT_ENTRY(mp_slab_exit,         mp_slab_module_exit),
    MODULE_EXIT_ENTRY(mp_fixed_exit_th,     mp_fixed_module_exit_th),
    MODULE_EXIT_ENTRY(el_exit_th,           ev_loop_module_exit_th),
    MODULE_EXIT_ENTRY(timer_exit,           ev_timer_module_exit),
};

// 公共模块退出函数集合，bottom half，进程自身退出函数后调用
static const module_exit_entry_t module_exit_common_table[] = {
    // TODO: 
    MODULE_EXIT_ENTRY(thread_pool_exit,     thp_module_exit),
    MODULE_EXIT_ENTRY(mp_nonfxied_exit_bh,  mp_nonfixed_module_exit_bh),
};

// core进程模块初始化函数集合。fork后执行，仅core进程模块调用
static const module_init_entry_t module_init_core_table[] = {
    MODULE_INIT_ENTRY(syslog_init,          syslog_module_init),
    MODULE_INIT_ENTRY(zcap_init,            zcap_module_init),
    MODULE_INIT_ENTRY(ftx_init,             ftx_module_init),
    MODULE_INIT_ENTRY(core_i,               core_iso_init),
    MODULE_INIT_ENTRY(arp_init,             arp_module_init),
    MODULE_INIT_ENTRY(ping_init,            ping_module_init),
    MODULE_INIT_ENTRY(tftp_init,            tftp_module_init),
    MODULE_INIT_ENTRY(cmcp_init,            cmcp_module_init),
    MODULE_INIT_ENTRY(web_init,             web_module_init),
    MODULE_INIT_ENTRY(led_init,             led_module_init),
    MODULE_INIT_ENTRY(sht30_init,           sht30_module_init),
    MODULE_INIT_ENTRY(ssd1306_init,         ssd1306_module_init),
    MODULE_INIT_ENTRY(sysmon_init,          sysmon_module_init),
    MODULE_INIT_ENTRY(cli_init,             cli_module_init),
};

// core进程退出函数集合，逆序init退出
static const module_exit_entry_t module_exit_core_table[] = {
    MODULE_EXIT_ENTRY(cli_exit_th,          cli_module_exit_th),
    MODULE_EXIT_ENTRY(zcap_exit_th,         zcap_module_exit_th),
    MODULE_EXIT_ENTRY(cli_exit_bh,          cli_module_exit_bh),
    MODULE_EXIT_ENTRY(sysmon_exit,          sysmon_module_exit),
    MODULE_EXIT_ENTRY(ssd1306_exit,         ssd1306_module_exit),
    MODULE_EXIT_ENTRY(sht30_exit,           sht30_module_exit),
    MODULE_EXIT_ENTRY(led_exit,             led_module_exit),
    MODULE_EXIT_ENTRY(web_exit,             web_module_exit),
    MODULE_EXIT_ENTRY(cmcp_exit,            cmcp_module_exit),
    MODULE_EXIT_ENTRY(tftp_exit,            tftp_module_exit),
    MODULE_EXIT_ENTRY(ping_exit,            ping_module_exit),
    MODULE_EXIT_ENTRY(arp_exit,             arp_module_exit),
    MODULE_EXIT_ENTRY(core_e,               core_iso_exit),
    MODULE_EXIT_ENTRY(ftx_exit,             ftx_module_exit),
    MODULE_EXIT_ENTRY(zcap_exit_bh,         zcap_module_exit_bh),
    MODULE_EXIT_ENTRY(syslog_exit,          syslog_module_exit),
};

// 进程入口函数集合
static const process_entry_t process_entry_table[] = {
    PROCESS_ENTRY(PID_RPC,          rpc_broker,         rpc_proc_entry),
    PROCESS_ENTRY(PID_TEST,         test,               test_proc_entry),
};

// 进程退出标志shm_name集合，共享内存，core写入0来结束进程
static const char* process_running_table[] = {
    RPC_PROCESS_RUNNING_SHM_NAME,                   // rpc进程
    TEST_PROCESS_RUNNING_SHM_NAME,                  // test
};

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static void platform_init()
{
    // core进程命名方便调试
    prctl(PR_SET_NAME, "CORE");

    //debug_level_set(debug_level_all);
    // top half
    for(int i = 0; i < array_size(module_init_th_common_table); ++ i)
    {
        module_init_th_common_table[i].func();
        dbg_always("init func <%s> called", module_init_th_common_table[i].name);
    }

    // process init
    for(int i = 0; i < array_size(process_entry_table); ++ i)
    {
        pid_t pid = fork();
        if(pid < 0)
        {
            dbg_error("fork for index %d fail", i);
        }
        else if(pid == 0)       // child process
        {
            // 设置进程名称
            prctl(PR_SET_NAME, process_entry_table[i].name);
            // 子进程忽略SIGINT，退出时由core进程通知退出
            signal(SIGINT, SIG_IGN);
            // 子进程设置逻辑PID
            proc_id_set(process_entry_table[i].pid);

            // child common bottom half
            int idx0 = 0;
            for(; idx0 < array_size(module_init_bh_common_table); ++ idx0)
            {
                module_init_bh_common_table[idx0].func();
                dbg_always("init func <%s> called", module_init_bh_common_table[idx0].name);
            }

            process_entry_table[i].func();

            // 线程结束，模块exit
            // common th
            for(int idx0 = 0; idx0 < array_size(module_exit_th_common_table); ++ idx0)
                module_exit_th_common_table[idx0].func();
            // process exit

            // common bh
            int idx1 = 0;
            for(; idx1 < array_size(module_exit_common_table); ++ idx1)
                module_exit_common_table[idx1].func();

            exit(0);        // 进程执行完毕退出
        }
    }

    // 设置pid core
    proc_id_set(PID_CORE);

    // core common bottom half
    for(int i = 0; i < array_size(module_init_bh_common_table); ++ i)
    {
        module_init_bh_common_table[i].func();
        dbg_always("init func <%s> called", module_init_bh_common_table[i].name);
    }

    // core init
    for(int i = 0; i < array_size(module_init_core_table); ++ i)
    {
        module_init_core_table[i].func();
        dbg_always("init func <%s> called", module_init_core_table[i].name);
    }

    //debug_level_set(debug_level_none);

    dbg_major("core process init done");
}

static void _proc_exit()
{
    shm_mgnt_t *shm;
    int run = 0;
    for(int i = 0; i < array_size(process_running_table); ++ i)
    {
        shm = shm_mgnt_open(process_running_table[i], sizeof(run), 0);
        if(likely(shm))
        {
            shm_mgnt_write(shm, 0, &run, sizeof(run));
            shm_mgnt_close(shm);
            shm = NULL;
        }
    }
}

static void platform_exit()
{
    //debug_level_set(debug_level_all);

    // exit other procs
    _proc_exit();

    // common exit th
    for(int i = 0; i < array_size(module_exit_th_common_table); ++ i)
    {
        module_exit_th_common_table[i].func();
        dbg_always("exit func <%s> called", module_exit_th_common_table[i].name);
    }

    // core app exit
    int idx0 = 0;
    for(; idx0 < array_size(module_exit_core_table); ++ idx0)
    {
        module_exit_core_table[idx0].func();
        dbg_always("exit func <%s> called", module_exit_core_table[idx0].name);
    }

    // common exit
    int idx1 = 0;
    for(; idx1 < array_size(module_exit_common_table); ++ idx1)
    {
        module_exit_common_table[idx1].func();
        dbg_always("exit func <%s> called", module_exit_common_table[idx1].name);
    }

    //debug_level_set(debug_level_none);
}

static void signal_init()
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if(sigaction(SIGINT, &sa, NULL) < 0)
        dbg_error("sigaction for SIGINT fail");
}

static inline void signal_handler(int signo)
{
    if(SIGINT == signo)
        ATOM_STORE(&core_running, 0, MORDER_RELAXED);
}

static void core_iso_init()
{
    // 启动抓包
    zcap_register(eth0);
    zcap_start(eth0);
    zcap_register(wlan0);
    zcap_start(wlan0);
    // 启动发包
    declare_ftx(eth0);
    declare_ftx(wlan0);

    // 注册rpc函数
    rpc_server_func_register("printf_hello", _rpc_printf_hello);
}

static void core_iso_exit()
{
    // 关闭发包
    undeclare_ftx(eth0);
    undeclare_ftx(wlan0);
    // 关闭抓包，回收工作交给zcap_module_exit
    zcap_cancel(eth0);
    zcap_cancel(wlan0);
}

/**
 * @brief       core进程入口
 */
int main()
{
    // 注册信号处理
    signal_init();

    safe_printf("\nWelcome~^v^ Init start...\n\n");

    // 平台模块初始化
    platform_init();

    ev_sem_post(&g_platform_init_ok);
    ATOM_STORE(&core_running, 1, MORDER_RELAXED);
    // Core Main进入休眠
    while(ATOM_LOAD(&core_running, MORDER_RELAXED))
        sleep(1);       // 进行心跳监控

    // 处理core exit
    platform_exit();

    safe_printf("\nBye~^v^\n");

    return 0;
}