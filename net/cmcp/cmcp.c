/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    cmcp.c
 * @brief   CMCP实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-09-02
 * @version 1.0
 *
 * @note    CMCP: Cai Misc-C Communicate Protocol，练习自定义协议实现
 *
 * @history
 *   1.0 | 2026-09-02 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <pthread.h>

#include "cmcp/cmcp.h"
#include "plat/debug.h"
#include "cli/cli.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

extern void* _cmcp_fsm(void *args);
extern void cmcp_fsm_msg_push(cmcp_fsm_msg_t *msg);
extern void cmcp_pkt_init();

/**
 * @brief       cli hook for: cmcp on
 */
static attr_force_inline void* _cmcp_on_cli_hook(unsigned char argc, char *argv[])
{
    cmcp_fsm_msg_t msg = {.event = CMCP_EVENT_START};
    cmcp_fsm_msg_push(&msg);
    return NULL;
}

/**
 * @brief       cli hook for: cmcp off
 */
static attr_force_inline void* _cmcp_off_cli_hook(unsigned char argc, char *argv[])
{
    cmcp_fsm_msg_t msg = {.event = CMCP_EVENT_ABORT};
    cmcp_fsm_msg_push(&msg);
    return NULL;
}

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// fsm tid
static pthread_t g_mcp_fsm_tid;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void mcp_module_init()
{
    // 初始化报文相关
    cmcp_pkt_init();

    // 创建状态机线程
    pthread_create(&g_mcp_fsm_tid, NULL, _cmcp_fsm, NULL);

    // 注册cli用于开关cmcp服务
    cli_register("cmcp on", "enable cmcp", NULL, _cmcp_on_cli_hook);
    cli_register("cmcp off", "disable cmcp", NULL, _cmcp_off_cli_hook);

    dbg_major("mcp module init ok");
}