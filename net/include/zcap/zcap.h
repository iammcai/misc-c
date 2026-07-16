/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    zcap.h
 * @brief   抓包头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-25
 * @version 1.1
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-11 | cai | Initial creation.
 *   1.1 | 2026-06-25 | cai | Use event_loop and hash handle for analyzing pkt.
 */

#ifndef __ZCAP_H__
#define __ZCAP_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <pthread.h>
#include "plat/compiler.h"
#include "plat/atom.h"
#include "type/type_atom_queue.h"
#include "msg_q/msg_q.h"
#include "type/type_hash.h"
#include "event/ev_lock.h"
#include "net.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 帧大小，2KB，足够满足普通以太网报文
#define FRAME_SIZE      (2048)

// mac地址长度
#define MAC_ADDR_SIZE   (6)

#define ZCAP_HASH_Q_SIZE        (4)     // hash分片处理报文的队列数量，必须2的幂次

// 过滤字段长度
#define FIELD_DATA_SIZE     (6)
// 过滤掩码长度
#define FIELD_MASK_SIZE     (6)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 匹配filter执行的回调
typedef void (*zcap_pkt_filter_match_cb)(char *packet, int len, void *args);

// 报文条件注册字段枚举
typedef enum{
    MAC_DA,     // 目的mac
    MAC_SA,     // 源mac
    LEN_TYPE,   // ether type
    IP_DA,      // 目的ip
    IP_SA,      // 源ip
    PRO_TYPE,   // ip protocol
    DPORT,      // 目的端口号
    SPORT,      // 源端口号
}zcap_pkt_field_e;

// 报文注册字段信息，与zcap_packet_field_e对应
typedef struct{
    uint8_t mac_da[MAC_ADDR_SIZE];
    uint8_t mac_sa[MAC_ADDR_SIZE];
    uint16_t ether_type;
    uint32_t ip_da;
    uint32_t ip_sa;
    uint8_t ip_pro;
    uint16_t l4_dport;
    uint16_t l4_sport;
}zcap_pkt_info_t;

// 预定义field链表，存储过滤项包含的字段
pre_declare_list(zcap_pkt_field)

// 报文过滤field定义
typedef struct{
    zcap_pkt_field_e field;
    uint8_t data[FIELD_DATA_SIZE];      // 最多需要6B来记录
    uint8_t mask[FIELD_MASK_SIZE];      // 掩码
    zcap_pkt_field_list_item_t item;    // 链表item
}zcap_pkt_field_t;

// 预定义报文过滤项哈希表，存储所有注册的过滤项
pre_declare_hash(zcap_pkt_filter)

// 报文过滤项定义
typedef struct{
    const char *name;       // 名称
    int enable;             // 是否开启
    zcap_pkt_field_list_head_t fields;  // 存储fields的链表
    zcap_pkt_filter_match_cb cb;        // 匹配执行的回调
    void *args;         // 回调入参
    zcap_pkt_filter_hash_item_t item;   // filter哈希表item
    
    ATOMIC_UINT64_T match_cnt;      // 命中次数
}zcap_pkt_filter_t;

// 预定义hash，用于存zcap_t
pre_declare_hash(zcaptor)

// 报文统计结构
typedef struct{
    ev_mutex_t mtx;             // 互斥锁
    unsigned long count;        // 总数量
    unsigned long size;         // 总大小，单位Byte

    // 报文类型
    unsigned long err;          // 错包数量

    //以下用于速率统计
    unsigned long curr_count;   // 本次统计数量
    unsigned long curr_size;    // 本次统计大小
    unsigned long last_count;   // 上次统计数量
    unsigned long last_size;    // 上次统计大小
    double rate_pps;            // rx pps
    double rate_Bps;            // rx Bps
}zcap_stat_t;

// 预定义spsc_aq，用于存放alz_t和rx_t交互报文
pre_declare_spsc_atom_queue(zcap_packet)

// 报文五元组信息定义
typedef struct{
    uint32_t src_ip;    // ipv4 or ipv6 L32 bit
    uint32_t dst_ip;
    uint32_t src_port;
    uint32_t dst_port;
    uint8_t protocol;
    uint8_t err_pkt;    // 是否错包
} attr_packed zcap_pakcet_flow_key_t;

// 报文定义
typedef struct{
    unsigned int len;           // 报文长度
    zcap_packet_spsc_atom_queue_item_t item;    // aq item
    zcap_pakcet_flow_key_t flow_key;    // 五元组信息
    char packet[];    // 报文内容
}zcap_packet_t;

// 解析报文结构
typedef struct{
    pthread_t alz_t;    // 线程ID
    zcap_packet_spsc_atom_queue_head_t alz_aq_head; // 接收的队列
    ev_sem_t alz_sem;   // 用于唤醒
}zcap_packet_alz_t;

// 抓包结构定义
typedef struct{
    const char *if_name;    // 监听接口名
    int sock_fd;            // 监听所用socket fd
    int if_index;           // 接口对应内核index
    uint8_t if_mac[MAC_ADDR_SIZE];  // 接口mac
    ATOMIC_UINT8_T ready;   // 是否初始化完毕
    void *ring_buffer;      // 映射
    unsigned long ring_len; // 映射大小

    pthread_t rx_t;         // 抓包线程
    ATOMIC_UINT8_T running; // 启动标志

    zcap_packet_alz_t alz[ZCAP_HASH_Q_SIZE];    // 解析结构

    zcap_stat_t stat;           // 统计数据

    zcap_packet_spsc_atom_queue_head_t aq_head; // 用于rx_t传递报文给ev_loop_cb

    int event_fd;               // 内核计数器，用于通知进行解析报文

    zcap_pkt_filter_hash_head_t filters;    // 管理注册的过滤条件的哈希表

    zcaptor_hash_item_t item;   // hash item
}zcap_t;

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/**
 *  外部使用，声明抓包，然后初始化
 * 1. 定义全局static zcap_t 类型变量
 * 2. 执行初始化函数
 */
#define declare_zcap(_ifname) \
static zcap_t _ifname ## _zcaptor = {   \
    .if_name = #_ifname,    \
    .sock_fd = -1,  \
    .if_index = -1, \
    .ring_buffer = NULL,    \
    .ring_len = 0,  \
    .ready = 0, \
    .rx_t = 0,  \
    .running = 0,   \
    .stat.mtx = EV_MUTEX_INITIALIZER,   \
    .event_fd = -1, \
    .alz = {},  \
};  \
static void _ifname ## _zcap_init(void) attr_ctor(CTOR_PRIO_MID);   \
static void _ifname ## _zcap_init(void) \
{   \
    _zcap_init(&_ifname ## _zcaptor);   \
}   \
/* _zcap_init end */

/**
 * 外部使用，启动抓包
 */
#define zcap_start(_ifname) \
    _zcap_start(&_ifname ## _zcaptor);  \
/* zcap_start end */

/**
 * 外部使用，结束抓包
 */
#define zcap_cancel(_ifname)    \
    _zcap_cancel(&_ifname ## _zcaptor);
/* zcap_cancel end */

/**
 * 外部使用，注册过滤项
 */
#define zcap_register_pkt_filter(_ifname, _filter_name, _cb, _args) \
    _zcap_register_pkt_filter(_ifname, _filter_name, _cb, _args);   \
/* zcap_register_pkt_filter end */

/**
 * 内部使用，注册过滤字段
 */
#define zcap_register_field(_ifname, _filter_name, field_type, data, mask)  \
    _zcap_register_field(_ifname, _filter_name, field_type, data, mask);    
/* zcap_register_field end */

/**
 * 外部使用，注册len type字段
 */
#define zcap_register_field_len_type(_ifname, _filter_name, _data, _mask) do  { \
    uint8_t data[FIELD_DATA_SIZE];  \
    uint8_t mask[FIELD_MASK_SIZE];  \
    data[0] = (_data >> 8) & 0xff;  \
    data[1] = (_data >> 0) & 0xff;  \
    mask[0] = (_mask >> 8) & 0xff;  \
    mask[1] = (_mask >> 0) & 0xff;  \
    zcap_register_field(_ifname, _filter_name, LEN_TYPE, data, mask);   \
}while(0);  \

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       内部使用，初始化抓包组件，未启动
 * 
 * @param[in]   captor  - captor
 */
extern void _zcap_init(zcap_t *captor);

/**
 * @brief       启动抓包线程
 * 
 * @param[in]   captor  - captor
 */
extern void _zcap_start(zcap_t *captor);

/**
 * @brief       结束抓包线程
 * 
 * @param[in]   captor  - captor
 */
extern void _zcap_cancel(zcap_t *captor);

/**
 * @brief       register filter into zcaptor
 * 
 * @param[in]   if_name         - if name
 * @param[in]   filter_name     - filter name
 * @param[in]   cb              - match cb
 * @param[in]   args            - cb args
 */
extern void _zcap_register_pkt_filter(const char *if_name, const char *filter_name, zcap_pkt_filter_match_cb cb, void *args);

/**
 * @brief       register field into filter
 * 
 * @param[in]   if_name     - if name
 * @param[in]   filter_name - filter name
 * @param[in]   field       - field of pkt
 * @param[in]   data        - field data
 * @param[in]   mask        - field mask
 */
extern void _zcap_register_field(const char *if_name, const char *filter_name, zcap_pkt_field_e field, uint8_t *data, uint8_t *mask);

#endif
/* __ZCAP_H__ end */