/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    zcap_internal.h
 * @brief   zcap内部头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-11-05
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-11-05 | cai | Initial creation.
 */

#ifndef __ZCAP_INTERNAL_H__
#define __ZCAP_INTERNAL_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <linux/if_ether.h>     // for ETH_P_ALL
#include <netinet/in.h>         // for htons
#include <netinet/ip.h>         // for struct iphdr

#include "zcap/zcap.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 帧大小，2KB，足够满足普通以太网报文
#define FRAME_SIZE      (2048)

// 监听接口名长度
#define ZCAP_IF_NAME_SIZE   (16)

// hash分片处理报文的队列数量，必须2的幂次
#define ZCAP_HASH_Q_SIZE    (4)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

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

// 报文方向
typedef enum{
    ZCAP_PACKET_INGRESS = 0,
    ZCAP_PACKET_EGRESS,

    ZCAP_PACKET_DIRECTION_MAX,      // 计数
}zcap_packet_direction_e;

// 报文定义
typedef struct{
    zcap_packet_direction_e direction;      // 方向
    unsigned int len;           // 报文长度
    zcap_packet_spsc_atom_queue_item_t item;    // aq item
    zcap_pakcet_flow_key_t flow_key;    // 五元组信息
    char packet[];    // 报文内容
}zcap_packet_t;

// 线程解析数据定义，每个解析线程更新自己的，无锁。全局直接叠加统计，允许一定误差
typedef struct{
    uint64_t counts;        // 收包数量
    uint64_t bytes;         // 收包字节数
    // cast类型
    uint64_t uc_count;      // 单播报文个数
    uint64_t mc_count;      // 组播报文个数
    uint64_t bc_count;      // 广播报文个数
    // 报文大小
    uint64_t size_lt_64;    // 小于64B的数量
    uint64_t size_64_128;
    uint64_t size_128_256;
    uint64_t size_256_512;
    uint64_t size_512_1024;
    uint64_t size_1024_1518;
    uint64_t size_gt_1518;
    // 协议统计数量
    uint64_t arp;           // arp
    uint64_t icmp;          // icmp
    uint64_t miscc_web;     // tcp 8080
    uint64_t ssh;           // tcp 22
    uint64_t dns;           // udp 53
    uint32_t dhcp;          // udp 67/68
    uint64_t tcp_other;
    uint64_t udp_other;
    uint64_t others;
    // tcp统计
    tcp_dissect_summary_t tcp;      // tcp解析数据
} attr_aligned(64) zcap_alz_stats_t;        // 按照64B对齐，避免同一个cache line伪共享，多个线程会干扰

// 解析报文结构
typedef struct{
    pthread_t alz_t;    // 线程ID
    zcap_packet_spsc_atom_queue_head_t alz_aq_head; // 接收的队列
    ev_sem_t alz_sem;   // 用于唤醒
    zcap_alz_stats_t rx_stats;     // rx统计数据
    zcap_alz_stats_t tx_stats;     // tx统计数据
}zcap_packet_alz_t;

// 预定义流量来源哈希表节点
pre_declare_hash(zcap_src_top)
// heap定义
pre_declare_heap(zcap_src_top)

// 流量来源条目定义
typedef struct{
    uint8_t smac[MAC_ADDR_SIZE];
    uint64_t cnt;

    zcap_src_top_hash_item_t hash_item;     // hash item
    zcap_src_top_heap_item_t heap_item;     // heap item
}zcap_top_src_entry_t;

// 处理流量来源top的线程结构定义
typedef struct{
    pthread_t tid;      // 线程tid
    ATOMIC_UINT8_T run;
    msg_q_t *msgq;      // 消息队列，解析线程发，本线程收
    zcap_src_top_hash_head_t hashtable;     // 哈希表，存储流量来源信息
    ev_spinlock_t lock;     // 用于并发，写多读少
}zcap_top_cal_thd_t;

// 全局统计数据定义
typedef struct{
    uint64_t last_counts;
    uint64_t last_bytes;
    double pps;
    double Bps;
    ev_spinlock_t lock;
}zcap_stats_t;

// 抓包结构定义
struct zcap_s{
    char if_name[ZCAP_IF_NAME_SIZE];    // 监听接口名
    int sock_fd;            // 监听所用socket fd
    int if_index;           // 接口对应内核index
    uint8_t if_mac[MAC_ADDR_SIZE];  // 接口mac
    ATOMIC_UINT8_T ready;   // 是否初始化完毕
    void *ring_buffer;      // 映射
    unsigned long ring_len; // 映射大小

    pthread_t rx_t;         // 抓包线程
    ATOMIC_UINT8_T running; // 启动标志

    zcap_packet_alz_t alz[ZCAP_HASH_Q_SIZE];    // 解析线程相关

    zcap_stats_t stats[ZCAP_PACKET_DIRECTION_MAX];  // 全局统计数据

    zcap_top_cal_thd_t top;     // src top相关

    zcap_pkt_filter_hash_head_t filters;    // 管理注册的过滤条件的哈希表

    zcaptor_hash_item_t item;   // hash item
};

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */
//

/**
 * @brief       cmp between zcaptors
 * 
 * @param[in]   z1  - captor 1
 * @param[in]   z2  - captor 2
 * 
 * @retval      cmp val
 * 
 * @note        cmp by if_name
 */
static attr_pure_inline int _zcaptor_cmp(zcap_t *z1, zcap_t *z2)
{
    assert(z1 && z1->if_name && z2 && z2->if_name);
    return strcmp(z1->if_name, z2->if_name);
}

/**
 * @brief       hash zcaptor
 * 
 * @param[in]   z   - captor
 * 
 * @retval      hash val
 * 
 * @note        hash by if_name
 */
static attr_pure_inline int _zcaptor_hash(zcap_t *z)
{
    assert(z && z->if_name);
    return type_hash_jhash(z->if_name, strlen(z->if_name), 0);
}

// 定义哈希表操作
declare_hash(zcaptor, zcaptor, zcap_t, item, 1, 4, _zcaptor_cmp, _zcaptor_hash);

/**
 * @brief       extract packet flow key
 * 
 * @param[in]   packet  - ptr to packet info
 * 
 * @note        提取五元组信息，用于后续哈希分流到线程池处理
 */
extern void _zcap_packet_flow_key_extract(zcap_packet_t *packet);

/**
 * @brief       统计报文长度
 * 
 * @param[in]   stats       - stats, alz线程独立
 * @param[in]   packet      - packet
 */
extern void _zcap_stats_count_handle(zcap_alz_stats_t *stats, zcap_packet_t *packet);

/**
 * @brief       统计报文长度
 * 
 * @param[in]   stats       - stats, alz线程独立
 * @param[in]   packet      - packet
 */
extern void _zcap_stats_size_handle(zcap_alz_stats_t *stats, zcap_packet_t *packet);

/**
 * @brief       统计报文uc/mc/bc信息
 * 
 * @param[in]   stats       - stats, alz线程独立
 * @param[in]   packet      - packet
 */
extern void _zcap_stats_cast_handle(zcap_alz_stats_t *stats, zcap_packet_t *packet);

/**
 * @brief       handle protocol and add count
 * 
 * @param[in]   stats       - stats
 * @param[in]   packet      - packet info
 * 
 * @note        解析报文协议，添加协议rx个数，线程独立
 */
extern void _zcap_stats_protocol_handle(zcap_alz_stats_t *stats, zcap_packet_t *packet);

/**
 * @brief       handle src top 
 * 
 * @param[in]   captor
 * @param[in]   src_mac
 * 
 * @note        往top消息队列发信息
 */
extern void _zcap_stats_src_top_handle(zcap_t *captor, char *src_mac);

/**
 * @brief       start zcap tx rate caculate
 * 
 * @note        启动tx实时速率计算
 */
extern void _zcap_tx_rate_cal_start();

/**
 * @brief       stop zcap tx rate caculate
 * 
 * @note        结束tx实时速率计算
 */
extern void _zcap_tx_rate_cal_stop();

#endif