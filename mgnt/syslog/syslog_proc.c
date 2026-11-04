/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    syslog_proc.c
 * @brief   处理日志进程相关
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-11-03
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-11-03 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdio.h>
#include <unistd.h>

#include "syslog/syslog_internal.h"
#include "ipc/shm/shm.h"
#include "rpc/rpc.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void syslog_proc_entry()
{
    // TODO: 初始化考虑抽象成公共接口
    shm_mgnt_t *syslog_running = shm_mgnt_open(SYSLOG_PROCESS_RUNNING_SHM_NAME, sizeof(int), 1);
    pfm_ensure_ret_void(syslog_running);
    int run = 1;
    shm_mgnt_write(syslog_running, 0, &run, sizeof(run));

    // 注册rpc
    rpc_server_func_register("syslog_add_rpc_server", _syslog_add_rpc_server);
    rpc_server_func_register("syslog_flush_rpc_server", _syslog_flush_rpc_server);

    // 启动刷盘定时器
    _syslog_store_flash_timer_start();

    sleep(5);       // 等待core

    do{
        shm_mgnt_read(syslog_running, 0, &run, sizeof(run));
        if(unlikely(!run))
            break;

        usleep(100 * 1000);
    }while(1);

    _syslog_store_flash_timer_end();

    shm_mgnt_close(syslog_running);

    dbg_always("@@@ syslog process exit");
}