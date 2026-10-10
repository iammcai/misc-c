/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    test.c
 * @brief   测试多进程
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-09-28
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-09-28 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <unistd.h>

#include "test_proc/test.h"
#include "plat/debug.h"
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

void test_proc_entry()
{
    shm_mgnt_t *test_running = shm_mgnt_open(TEST_PROCESS_RUNNING_SHM_NAME, sizeof(int), 1);
    pfm_ensure_ret_void(test_running);
    int run = 1;
    shm_mgnt_write(test_running, 0, &run, sizeof(run));

    do{
        shm_mgnt_read(test_running, 0, &run, sizeof(run));
        if(unlikely(!run))
            break;
        
        // rpc_call_func_async(PID_CORE, "printf_hello", NULL);
        usleep(1000 * 1000);
    }while(1);

    dbg_always("@@@ test process exit");
}