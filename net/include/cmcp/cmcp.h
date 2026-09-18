/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    cmcp.h
 * @brief   CMCP头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-09-02
 * @version 1.0
 *
 * @note    MCP: Cai Misc-C Communicate Protocol，练习自定义协议实现
 *
 * @history
 *   1.0 | 2026-09-02 | cai | Initial creation.
 */

#ifndef __CMCP_H__
#define __CMCP_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdint.h>

#include "plat/compiler.h"
#include "msg_q/msg_q.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// CMCP事件枚举
typedef enum{
    CMCP_EVENT_START,           // cmcp启动
    CMCP_EVENT_ABORT,           // cmcp终止
    CMCP_EVENT_HELLO_RX,        // 收到hello报文

    CMCP_EVENT_COUNT,           // 计数
}cmcp_event_e;

// cmcp消息
typedef struct{
    cmcp_event_e event;         // 事件
}cmcp_fsm_msg_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       init mcp module
 */
extern void mcp_module_init();

#endif
/* __CMCP_H__ end */