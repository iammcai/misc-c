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

#include "plat/compiler.h"
#include "plat/debug.h"

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

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 模块初始化注册项生成
#define MODULE_INIT_ENTRY(n, f)     {#n, f}

// 进程入口表项生成
#define PROCESS_ENTRY(n, f)         {#n, f}

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 模块初始化函数签名
typedef void (*module_init_f)(void);
// 进程入口函数签名
typedef void (*process_entry_f)(void);

// 模块初始化表项定义
typedef struct{
    const char *name;
    module_init_f func;
}module_init_entry_t;

// 进程入口表项定义
typedef struct{
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
 * @brief       core进程内部一些初始化
 */
static void core_init();

/* ========================================================================== */
/*                             Extern Symbols                                 */
/* ========================================================================== */

// core运行标志
static ATOMIC_UINT8_T core_running;

// 用于通知platform init ok
ev_sem_t g_platform_init_ok;

declare_zcap(eth0)
declare_zcap(wlan0)

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
    MODULE_INIT_ENTRY(mp_nonfixed_th,       mp_nonfixed_module_init_th),
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
};

// core进程模块初始化函数集合。fork后执行，仅core进程模块调用
static const module_init_entry_t module_init_core_table[] = {
    MODULE_INIT_ENTRY(syslog_init,          syslog_module_init),
    MODULE_INIT_ENTRY(zcap_init,            zcap_module_init),
    MODULE_INIT_ENTRY(ftx_init,             ftx_module_init),
    MODULE_INIT_ENTRY(core_i,               core_init),
    MODULE_INIT_ENTRY(arp_init,             arp_module_init),
    MODULE_INIT_ENTRY(ping_init,            ping_module_init),
    MODULE_INIT_ENTRY(tftp_init,            tftp_module_init),
    MODULE_INIT_ENTRY(arp_init,             arp_module_init),
    MODULE_INIT_ENTRY(cmcp_init,            mcp_module_init),
    MODULE_INIT_ENTRY(web_init,             web_module_init),
    MODULE_INIT_ENTRY(led_init,             led_module_init),
    MODULE_INIT_ENTRY(sht30_init,           sht30_module_init),
    MODULE_INIT_ENTRY(ssd1306_init,         ssd1306_module_init),
    MODULE_INIT_ENTRY(sysmon_init,          sysmon_module_init),
    MODULE_INIT_ENTRY(cli_init,             cli_module_init),
};

// 进程入口函数集合
static const process_entry_t process_entry_table[] = {
};

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static void platform_init()
{
    // core进程命名方便调试
    prctl(PR_SET_NAME, "CORE");

    debug_level_set(debug_level_all);
    // top half
    for(int i = 0; i < array_size(module_init_th_common_table); ++ i)
        module_init_th_common_table[i].func();

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
            // child common bottom half
            for(int i = 0; i < array_size(module_init_bh_common_table); ++ i)
                module_init_bh_common_table[i].func();

            process_entry_table[i].func();

            exit(0);        // 进程执行完毕退出
        }
    }

    // core common bottom half
    for(int i = 0; i < array_size(module_init_bh_common_table); ++ i)
        module_init_bh_common_table[i].func();

    // core init
    for(int i = 0; i < array_size(module_init_core_table); ++ i)
        module_init_core_table[i].func();

    debug_level_set(debug_level_none);

    dbg_major("core process init done");
}

static void core_init()
{
    // 启动抓包
    zcap_register(eth0);
    zcap_start(eth0);
    zcap_register(wlan0);
    zcap_start(wlan0);
    // 启动发包
    declare_ftx(eth0);
    declare_ftx(wlan0);
}

/**
 * @brief       core进程入口
 */
int main()
{
    // 平台模块初始化
    platform_init();

    ev_sem_post(&g_platform_init_ok);
    ATOM_STORE(&core_running, 1, MORDER_RELAXED);
    // Core Main进入休眠
    while(ATOM_LOAD(&core_running, MORDER_RELAXED))
        sleep(1);       // 进行心跳监控
    // TODO: deinit

    return 0;
}