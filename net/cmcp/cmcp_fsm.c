/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    cmcp_fsm.c
 * @brief   cmcp状态机实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-09-03
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-09-03 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <pthread.h>
#include <sys/prctl.h>

#include "cmcp/cmcp.h"
#include "plat/debug.h"
#include "swdg/swdg.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// CMCP消息队列长度
#define CMCP_FSM_MSGQ_CAP       (32)

// fsm等待消息时间
#define CMCP_FSM_MSG_WAIT       (1000)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// CMCP状态枚举
typedef enum{
    CMCP_STATE_IDLE,            // idle，尚未启用的状态
    CMCP_STATE_ABORT,           // 终止cmcp服务
    CMCP_STATE_MATCH,           // 匹配对端，尝试建立通信的状态
    CMCP_STATE_STABLE,          // stable，通信建立完毕后的状态

    CMCP_STATE_COUNT,           // 计数
}cmcp_state_e;

// 状态机管理定义
typedef struct{
    ATOMIC_UINT8_T running;     // 运行flag
    msg_q_t *msg_q;             // 消息队列
    cmcp_state_e state;         // 当前状态
}cmcp_fsm_mgnt_t;

// 状态enter回调函数签名
typedef void (*cmcp_fsm_enter_f)(cmcp_fsm_msg_t *msg);
// 状态poll回调函数签名
typedef void (*cmcp_fsm_poll_f)(cmcp_fsm_msg_t *msg);
// 状态exit回调函数签名
typedef void (*cmcp_fsm_exit_f)(cmcp_fsm_msg_t *msg);

// 状态机回调成员定义
typedef struct{
    cmcp_fsm_enter_f enter;
    cmcp_fsm_poll_f poll;
    cmcp_fsm_exit_f exit;
}cmcp_fsm_cb_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

static void cmcp_state_idle_poll(cmcp_fsm_msg_t *msg);
static void cmcp_state_abort_poll(cmcp_fsm_msg_t *msg);
static void cmcp_state_match_enter(cmcp_fsm_msg_t *msg);
static void cmcp_state_match_poll(cmcp_fsm_msg_t *msg);
static void cmcp_state_stable_poll(cmcp_fsm_msg_t *msg);

extern void _cmcp_tx_hello();

/**
 * @brief       init cmcp fsm variables
 */
static attr_force_inline void _cmcp_fsm_init();

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// cmcp state字符串，用于调试
static const char* cmcp_state_str[] = {
    "idle", "abort", "match", "stable", 
};

// cmcp event字符串，用于调试
static const char* cmcp_event_str[] = {
    "start", "abort", "hello_rx", 
};

// 状态机变量
static cmcp_fsm_mgnt_t g_cmcp_fsm_mgnt = {};

// 状态机回调函数表
static cmcp_fsm_cb_t g_cmcp_cb_table[CMCP_STATE_COUNT] = {
    [CMCP_STATE_IDLE] = {
        .enter = NULL,
        .poll = cmcp_state_idle_poll,
        .exit = NULL,
    },
    [CMCP_STATE_ABORT] = {
        .enter = NULL,
        .poll = cmcp_state_match_poll,
        .exit = NULL,
    },
    [CMCP_STATE_MATCH] = {
        .enter = cmcp_state_match_enter,
        .poll = cmcp_state_match_poll,
        .exit = NULL,
    },
    [CMCP_STATE_STABLE] = {
        .enter = NULL,
        .poll = cmcp_state_stable_poll,
        .exit = NULL,
    },
};

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

/**
 * @brief       push msg into fsm msgq
 * 
 * @note        外部推送消息给状态机的入口
 */
void cmcp_fsm_msg_push(cmcp_fsm_msg_t *msg)
{
    _msg_q_push(g_cmcp_fsm_mgnt.msg_q, msg, sizeof(cmcp_fsm_msg_t));
}

/**
 * @brief       poll func for idle state
 */
static void cmcp_state_idle_poll(cmcp_fsm_msg_t *msg)
{
    switch(msg->event)
    {
        case CMCP_EVENT_START:                          // 启动mcmp，尝试配对
            g_cmcp_fsm_mgnt.state = CMCP_STATE_MATCH;
            break;
        case CMCP_EVENT_ABORT:
            g_cmcp_fsm_mgnt.state = CMCP_STATE_ABORT;
            break;
        default:
            break;
    }
}

/**
 * @brief       poll func for abort state
 */
static void cmcp_state_abort_poll(cmcp_fsm_msg_t *msg)
{
    switch(msg->event)
    {
        default:
            break;
    }
}

/**
 * @brief       enter func for match state
 */
static void cmcp_state_match_enter(cmcp_fsm_msg_t *msg)
{
    // TODO: 投递发包消息，启动重传定时器
    _cmcp_tx_hello();
}

/**
 * @brief       poll func for match state
 */
static void cmcp_state_match_poll(cmcp_fsm_msg_t *msg)
{
    switch(msg->event)
    {
        case CMCP_EVENT_ABORT:
            g_cmcp_fsm_mgnt.state = CMCP_STATE_ABORT;
            break;
        default:
            break;
    }
}

/**
 * @brief       poll func for stable state
 */
static void cmcp_state_stable_poll(cmcp_fsm_msg_t *msg)
{
    switch(msg->event)
    {
        case CMCP_EVENT_ABORT:
            g_cmcp_fsm_mgnt.state = CMCP_STATE_ABORT;
            break;
        default:
            break;
    }
}

static inline void _cmcp_fsm_init()
{
    memset(&g_cmcp_fsm_mgnt, 0, sizeof(cmcp_fsm_mgnt_t));

    g_cmcp_fsm_mgnt.state = CMCP_STATE_IDLE;
    ATOM_STORE(&g_cmcp_fsm_mgnt.running, 1, MORDER_RELAXED);

    g_cmcp_fsm_mgnt.msg_q = msg_q_create("cmcp_fsm", CMCP_FSM_MSGQ_CAP, sizeof(cmcp_fsm_msg_t));
}

/**
 * @brief       cmcp fsm routine
 * 
 * @note        运行CMCP状态机
 */
void* _cmcp_fsm(void *args)
{
    prctl(PR_SET_NAME, "cmcp_fsm");
    swdg_register("cmcp_fsm", CMCP_FSM_MSG_WAIT * 3);

    _cmcp_fsm_init();

    while(ATOM_LOAD(&g_cmcp_fsm_mgnt.running, MORDER_RELAXED))
    {
        swdg_feed();

        cmcp_fsm_msg_t msg = {};
        msg_q_ret_type_e msg_ret = _msg_q_pop(g_cmcp_fsm_mgnt.msg_q, sizeof(cmcp_fsm_msg_t), msg_q_wait_timeout, CMCP_FSM_MSG_WAIT, &msg);
        if(msg_ret == msg_q_ret_non_msg)
            continue;

        // 处理消息
        cmcp_state_e last_state = g_cmcp_fsm_mgnt.state;
        g_cmcp_cb_table[g_cmcp_fsm_mgnt.state].poll(&msg);      // poll
        cmcp_state_e new_state = g_cmcp_fsm_mgnt.state;

        dbg_always("cmcp fsm event [%s], state [%s] -> [%s]", cmcp_event_str[msg.event], cmcp_state_str[last_state], cmcp_state_str[new_state]);

        if(new_state != last_state)
        {
            if(g_cmcp_cb_table[last_state].exit)
                g_cmcp_cb_table[last_state].exit(&msg);         // exit
            if(g_cmcp_cb_table[new_state].enter)
                g_cmcp_cb_table[new_state].enter(&msg);         // enter
        }
    }

    return NULL;
}