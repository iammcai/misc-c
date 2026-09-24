/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    cmcp_pkt.c
 * @brief   cmcp报文相关
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
#include "zcap/zcap.h"
#include "ftx/ftx.h"
#include "mp/mp_slab.h"
#include "plat/debug.h"
#include "msg_q/msg_q.h"
#include "swdg/swdg.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// CMCP etherType
#define CMCP_ETHER_TYPE     (0x1CC1)

// CMCP发包队列容量
#define CMCP_TX_MSGQ_CAP    (32)
// CMCP等待发包消息时间
#define CMCP_TX_MSGQ_WAIT   (1000)

// CMCP payload临时缓冲长度
#define CMCP_PKT_PAYLOAD_SIZE   (256)

// cmcp头部长度
#define PKTHDR_CMCP_SIZE        (sizeof(pkthdr_cmcp_t))

#define CMCP_TX_MSGQ_PUSH(pmsg)  _msg_q_push(g_cmcp_tx_msg_q, pmsg, sizeof(cmcp_tx_msg_t))

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// cmcp版本号
typedef enum{
    CMCP_PKT_VERSION_1 = 1,     // version 1
}cmcp_pkt_version_e;

// cmcp type，bit7
typedef enum{
    CMCP_PKT_TYPE_REQUEST = 0,
    CMCP_PKT_TYPE_REPLY = 1
}cmcp_pkt_type_e;

// cmcp session type, bit 6-0
typedef enum{
    CMCP_PKT_SESSION_TYPE_HELLO = 1,        // 握手报文
}cmcp_pkt_session_type_e;

// cmcp头部定义
typedef struct{
    uint8_t version;            // 版本号
    uint8_t type;               // bit7：0-请求，1-响应；bit6-0：事务类型
    uint16_t payload_len;       // payload长度
    uint32_t seq;               // 事务序列号
    uint16_t reserved;          // 保留，后续可能扩展为flags
    uint16_t checksum;          // 头部+payload校验和
}attr_packed pkthdr_cmcp_t;
_Static_assert(sizeof(pkthdr_cmcp_t) == 12, "sizeof pkthdr_cmcp_t must be 12 !");

// cmcp发包消息
typedef struct{
    char *buffer;       // 报文内容
    size_t size;        // 报文大小
}cmcp_tx_msg_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       cmcp收包回调
 */
static void _cmcp_zcap_cb(char *packet, int len, void *args);

/**
 * @brief       cmcp tx thread routine
 */
static void* _cmcp_tx(void *args);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 发包内存池
declare_mem_type_slab_extern(ftx)

// tx线程
static pthread_t g_cmcp_tx_tid;
// tx running
static ATOMIC_UINT8_T g_cmcp_tx_running;

// 消息队列，用于通知发包
static msg_q_t *g_cmcp_tx_msg_q = NULL;

// 全局seq，会话递增
static ATOMIC_UINT32_T g_cmcp_seq = 0;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

/**
 * @brief       send cmcp hello packet
 * 
 * @note        发送hello报文
 */
void _cmcp_tx_hello()
{
    uint8_t payload[CMCP_PKT_PAYLOAD_SIZE] = {};

    uint32_t seq = ATOM_FETCH_ADD(&g_cmcp_seq, 1, MORDER_ACQ_REL);

    int n = snprintf(payload, sizeof(payload), "Here is cmcp hello request %u", seq);
    int packet_size = sizeof(pkthdr_l2_t) + PKTHDR_CMCP_SIZE + n;

    char *packet = mp_slab_node_get(ftx, packet_size);
    if(unlikely(!packet))
        return;
    memset(packet, 0, packet_size);

    pkthdr_l2_t *l2 = (pkthdr_l2_t*)packet;
    l2->ether_type = htons(CMCP_ETHER_TYPE);
    memset(l2->mac_da, 0xff, MAC_ADDR_SIZE);
    ftx_mac_get("eth0", l2->mac_sa);

    pkthdr_cmcp_t *cmcp = (pkthdr_cmcp_t*)(packet + sizeof(pkthdr_l2_t));
    cmcp->version = CMCP_PKT_VERSION_1;
    cmcp->type = (CMCP_PKT_TYPE_REQUEST << 7) | (CMCP_PKT_SESSION_TYPE_HELLO & 0x3f);
    cmcp->payload_len = htons(n);
    cmcp->seq = htonl(seq);
    cmcp->reserved = 0;
    cmcp->checksum = 0;
    cmcp->checksum = calc_checksum(cmcp, n + PKTHDR_CMCP_SIZE);

    char *pd = (char*)cmcp + PKTHDR_CMCP_SIZE;
    memcpy(pd, payload, n);

    cmcp_tx_msg_t msg = {
        .buffer = packet,
        .size = packet_size,
    };
    CMCP_TX_MSGQ_PUSH(&msg);
}

static void* _cmcp_tx(void *args)
{
    prctl(PR_SET_NAME, "cmcp_tx");
    swdg_register("cmcp_tx", CMCP_TX_MSGQ_WAIT * 3);

    g_cmcp_tx_msg_q = msg_q_create("cmcp_tx", CMCP_TX_MSGQ_CAP, sizeof(cmcp_tx_msg_t));

    ATOM_STORE(&g_cmcp_tx_running, 1, MORDER_RELAXED);

    while(ATOM_LOAD(&g_cmcp_tx_running, MORDER_RELAXED))
    {
        swdg_feed();

        cmcp_tx_msg_t msg = {};
        msg_q_ret_type_e mq_ret = _msg_q_pop(g_cmcp_tx_msg_q, sizeof(cmcp_tx_msg_t), msg_q_wait_timeout, CMCP_TX_MSGQ_WAIT, &msg);
        if(mq_ret == msg_q_ret_non_msg)
            continue;

        // TODO: 接口发包选择
        ftx_send("eth0", msg.buffer, msg.size);
        ftx_send("wlan0", msg.buffer, msg.size);
    }

    // exit
    msg_q_destroy(&g_cmcp_tx_msg_q);
    swdg_deregister("cmcp_tx");

    return NULL;
}

static void _cmcp_zcap_cb(char *packet, int len, void *args)
{
    dbg_always("receive cmcp packet");
}

/**
 * @brief       init cmcp packets
 */
void cmcp_pkt_init()
{
    // 启动发包线程，监听msgq
    pthread_create(&g_cmcp_tx_tid, NULL, _cmcp_tx, NULL);

    // 注册zcap过滤条件
    zcap_register_pkt_filter("eth0", "cmcp", _cmcp_zcap_cb, NULL);
    zcap_register_field_len_type("eth0", "cmcp", CMCP_ETHER_TYPE, 0xffff);
    zcap_register_pkt_filter("wlan0", "cmcp", _cmcp_zcap_cb, NULL);
    zcap_register_field_len_type("wlan0", "cmcp", CMCP_ETHER_TYPE, 0xffff);
}

/**
 * @brief       cmcp packets exit
 */
void cmcp_pkt_exit()
{
    // 发包线程退出
    ATOM_STORE(&g_cmcp_tx_running, 0, MORDER_RELAXED);
    pthread_join(g_cmcp_tx_tid, NULL);
}