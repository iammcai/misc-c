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

#include "plat/compiler.h"
#include "plat/debug.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

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

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       init platform
 * 
 * @note        平台初始化
 */
static void platform_init();

/* ========================================================================== */
/*                             Extern Symbols                                 */
/* ========================================================================== */

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// TODO: 

// 公共模块初始化top half集合。fork前执行，要求不允许模块创建任何线程
static const module_init_entry_t module_init_th_common_table[] = {
};

// 公共模块初始化bottom half集合。fork后父子进程都执行，可创建任何线程
static const module_init_entry_t module_init_bh_common_table[] = {
};

// core进程模块初始化函数集合。fork后执行，仅core进程模块调用
static const module_init_entry_t module_init_core_table[] = {
};

// 进程入口函数集合
static const process_entry_f process_entry_table[] = {
};

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static void platform_init()
{
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
            // child common bottom half
            for(int i = 0; i < array_size(module_init_bh_common_table); ++ i)
                module_init_bh_common_table[i].func();

            process_entry_table[i]();

            exit(0);        // 进程执行完毕退出
        }
    }

    // core common bottom half
    for(int i = 0; i < array_size(module_init_bh_common_table); ++ i)
        module_init_bh_common_table[i].func();

    // core init
    for(int i = 0; i < array_size(module_init_core_table); ++ i)
        module_init_core_table[i].func();

    dbg_major("platform init done");
}

/**
 * @brief       core进程入口
 */
static int main()
{
    // 平台模块初始化
    platform_init();

    return 0;
}