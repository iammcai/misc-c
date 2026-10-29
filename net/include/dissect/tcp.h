/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    tcp.h
 * @brief   tcp解析头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-10-28
 * @version 1.0
 *
 * @note    接收zcap发来报文进行tcp解析
 *          依赖zcap同个流由同一个线程处理的特性，流表thread_local
 *          zcap alz需要单独初始化tcp_dissect
 *
 * @history
 *   1.0 | 2026-10-28 | cai | Initial creation.
 */

#ifndef __TCP_H__
#define __TCP_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdint.h>
#include <stddef.h>

#include "plat/compiler.h"
#include "type/type_hash.h"
#include "net_comm.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// tcp流表大小
#define TCP_FLOW_MAX_COUNT  (10240)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// tcp解析数据汇总定义
typedef struct{
    uint64_t total;             // tcp报文总数

    uint64_t _ack_total;        // 总ack数量，内部用

    uint64_t pure_syn;          // 纯syn，无ack，发起连接
    uint64_t syn_ack;           // 服务端同意连接
    uint64_t rst;               // 异常重置、拒绝
    uint64_t fin;               // 正常挥手关闭
    uint64_t psh_ack;           // PSH==1要求立即交付数据给应用层
    uint64_t non_psh_ack;       // PSH==0的数据报文，ack payload > 0，正常传输数据用
    uint64_t pure_ack;          // 纯ack，确认包
    uint64_t dup_ack;           // 重复ack数量，衡量网络质量
    uint64_t flag_invalid;      // tcp flag 异常组合
}tcp_dissect_summary_t;

// tcp流追踪状态
typedef struct{
    uint32_t last_seq;
    uint32_t last_ack;
    uint16_t last_window;
    uint16_t last_payload_len;
    uint64_t last_seen;         // 上次时间戳
    int is_initialized;
}tcp_ack_flow_state_t;

// 预定义哈希表节点
pre_declare_hash(tcp_ack_flow_entry)
// 预定义链表节点，链表用于临时记录需要老化的流
pre_declare_list(tcp_ack_flow_aging_entry)

// 哈希表节点定义
typedef struct{
    pkt_flow_5_t flow_key;              // 五元组，标识一条tcp流
    tcp_ack_flow_state_t state;             // 流状态

    tcp_ack_flow_entry_hash_item_t hash_item;    // hash item
    tcp_ack_flow_aging_entry_list_item_t list_item; // list item
}tcp_ack_flow_entry_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       dissect tcp packet
 * 
 * @param[in]   packet
 * @param[in]   len
 * @param[out]  tds     - tcp dissect summary data
 * 
 * @note        调用方确保传入packet合法，接口内不再校验
 */
extern void tcp_dissect(char *packet, size_t len, tcp_dissect_summary_t *tds);

/**
 * @brief       init tcp dissect
 * 
 * @note        call by each zcap alz thread
 */
extern void tcp_dissect_init();

/**
 * @brief       exit tcp dissect
 * 
 * @note        call by each zcap alz thread
 */
extern void tcp_dissect_exit();

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

#endif
/* __TCP_H__ end */