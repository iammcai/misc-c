/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    tcp.c
 * @brief   tcp解析实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-10-28
 * @version 1.0
 *
 * @note    eth0网卡随机丢弃5%的包，可以用来测试DUP ACK和重传
 *          sudo tc qdisc add dev eth0 root netem loss 5%       - set
 *          sudo tc qdisc show dev eth0                         - show
 *          sudo tc qdisc del dev eth0 root                     - del
 *
 * @history
 *   1.0 | 2026-10-28 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "dissect/tcp.h"
#include "net_comm.h"
#include "mp/mp_fixed.h"
#include "event/ev_timer.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 30s老化
#define tcp_ack_flow_entry_AGING_TIME       (30000)

// 处理几次tcp报文，触发一次流表老化
#define tcp_ack_flow_entry_AGING_BLCOK      (TCP_FLOW_MAX_COUNT >> 1)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

// 定义链表操作
declare_list(tcp_ack_flow_aging_entry, tcp_ack_flow_aging_entry, tcp_ack_flow_entry_t, list_item)

/**
 * @brief       cmp func for hashtable
 */
static attr_pure_inline int _tcp_ack_flow_entry_cmp(tcp_ack_flow_entry_t *left, tcp_ack_flow_entry_t *right)
{
    int ret = memcmp(&left->flow_key.src_ip, &right->flow_key.src_ip, sizeof(uint32_t));
    if(ret != 0)    return ret;
    ret = memcmp(&left->flow_key.src_port, &right->flow_key.src_port, sizeof(uint16_t));
    if(ret != 0)    return ret;
    ret = memcmp(&left->flow_key.dst_ip, &right->flow_key.dst_ip, sizeof(uint32_t));
    if(ret != 0)    return ret;
    ret = memcmp(&left->flow_key.dst_port, &right->flow_key.dst_port, sizeof(uint16_t));
    if(ret != 0)    return ret;
    return memcmp(&left->flow_key.protocol, &right->flow_key.protocol, sizeof(uint8_t));
}

/**
 * @brief       hash func for hashtable
 */
static attr_pure_inline unsigned int tcp_ack_flow_entry_hash(tcp_ack_flow_entry_t *entry)
{
    return type_hash_jhash(&entry->flow_key, sizeof(pkt_flow_5_t), 0);
}

// 定义hash操作
declare_hash(tcp_ack_flow_entry, tcp_ack_flow_entry, tcp_ack_flow_entry_t, hash_item, 1, 31, _tcp_ack_flow_entry_cmp, tcp_ack_flow_entry_hash)

// 内存池声明，在zcap.c定义
declare_mp_fixed_extern(dissect_tcp)

/**
 * @brief       get or create tcp flow entry
 * 
 * @param[in]   flow_key        - 五元组信息
 * 
 * @note        查找流表，没有的话新增
 */
static tcp_ack_flow_entry_t* tcp_ack_flow_entry_get_or_create(pkt_flow_5_t *flow_key);

/**
 * @brief       aging tcp flow entry
 * 
 * @param[in]   curr    - current time
 * 
 * @note        流表老化
 */
static void _tcp_flow_aging(uint64_t curr);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 线程独立ack流表
static thread_local tcp_ack_flow_entry_hash_head_t g_tcp_flow_hashtable = {};
// 线程独立链表，用于记录老化流，安全从哈希表中移除
static thread_local tcp_ack_flow_aging_entry_list_head_t g_tcp_aging_flow_list = {};
// 线程独立tcp计数
static thread_local uint64_t g_tcp_dissect_cnt = 0;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static tcp_ack_flow_entry_t* tcp_ack_flow_entry_get_or_create(pkt_flow_5_t *flow_key)
{
    pfm_ensure_ret(flow_key, NULL);

    tcp_ack_flow_entry_t *entry = NULL;

    // lock-free
    tcp_ack_flow_entry_t key = {};
    memcpy(&key.flow_key, flow_key, sizeof(pkt_flow_5_t));
    entry = tcp_ack_flow_entry_hash_find(&g_tcp_flow_hashtable, &key);
    if(!entry)
    {
        entry = mp_fixed_node_get(dissect_tcp);
        if(unlikely(!entry))
        {
            dbg_error("no mem for tcp flow entry");
            return NULL;
        }
        /*
        dbg_always("new entry for sip %08x dip %08x srcport %04x dstport %04x", 
            flow_key->src_ip, flow_key->dst_ip, flow_key->src_port, flow_key->dst_port, flow_key->protocol);
        */
        memcpy(&entry->flow_key, flow_key, sizeof(pkt_flow_5_t));
        entry->state.is_initialized = 0;        // 表示此前没有初始化
        // hash add
        tcp_ack_flow_entry_hash_add(&g_tcp_flow_hashtable, entry);
    }

    return entry;
}

static void _tcp_flow_aging(uint64_t curr)
{
    int aging_count = 0;

    // 先遍历流表，过期的加入list
    tcp_ack_flow_entry_t *flow = tcp_ack_flow_entry_hash_first(&g_tcp_flow_hashtable);
    while(flow)
    {
        if((curr > flow->state.last_seen) && (curr - flow->state.last_seen >= tcp_ack_flow_entry_AGING_TIME))
            tcp_ack_flow_aging_entry_list_add_head(&g_tcp_aging_flow_list, flow);
        flow = tcp_ack_flow_entry_hash_next(&g_tcp_flow_hashtable, flow);
    }
    // 遍历list，从流表中摘除，释放内存
    while(flow = tcp_ack_flow_aging_entry_list_pop(&g_tcp_aging_flow_list))
    {
        tcp_ack_flow_entry_hash_del(&g_tcp_flow_hashtable, flow);
        mp_fixed_node_put(flow);
        ++ aging_count;
    }
}

void tcp_dissect(char *packet, size_t len, tcp_dissect_summary_t *tds)
{
    pfm_ensure_ret_void(packet && len && tds);

    // 获取五元组信息
    pkthdr_ipv4_t *ipv4_hdr = (pkthdr_ipv4_t*)(packet + sizeof(pkthdr_l2_t));
    pkthdr_tcp_t *tcp_hdr = (pkthdr_tcp_t*)((char*)ipv4_hdr + IPV4_HEADER_SIZE(ipv4_hdr));

    uint8_t tcpflag = TCP_HDR_FLAG(tcp_hdr);
    uint16_t ip_total = IPV4_TOTAL_SIZE(ipv4_hdr);
    uint16_t ip_hdr_len = IPV4_HEADER_SIZE(ipv4_hdr);
    uint16_t tcp_hdr_len = TCP_HEADER_SIZE(tcp_hdr);
    if(unlikely(ip_total < ip_hdr_len + tcp_hdr_len))       // 畸形报文，概率比较低。这里防止unsigned下溢
        return;
    uint16_t tcp_payload_size = ip_total - ip_hdr_len - tcp_hdr_len;        // tcp负载长度

    tds->total ++;          // tcp总数+1

    /* ---------------- 处理tcp flag ---------------- */

    if(tcpflag & TCP_FLAG_ACK)
        tds->_ack_total ++;

    if((tcpflag & (TCP_FLAG_ACK | TCP_FLAG_SYN)) == TCP_FLAG_SYN)       // 纯syn包，表示发起连接
        tds->pure_syn ++;
    else if((tcpflag & (TCP_FLAG_ACK | TCP_FLAG_SYN)) == (TCP_FLAG_ACK | TCP_FLAG_SYN))     // syn+ack，表示服务器接收连接
        tds->syn_ack ++;
    else if(tcpflag & TCP_FLAG_RST)                                     // rst表示异常重置或者连接拒绝
        tds->rst ++;
    else if(tcpflag & TCP_FLAG_FIN)                                     // fin正常挥手关闭
        tds->fin ++;
    else if((tcpflag & (TCP_FLAG_ACK | TCP_FLAG_PSH)) == (TCP_FLAG_ACK | TCP_FLAG_PSH))     // ack+psh，表示紧急数据需要交付给应用层
        tds->psh_ack ++;
    else if((tcpflag == TCP_FLAG_ACK))                                  // 纯ack，或者不带psh的数据，比如大数据中间报文缓存
        if(tcp_payload_size == 0)
            tds->pure_ack ++;
        else
            tds->non_psh_ack ++;
    else                            // 异常tcp flag组合
        tds->flag_invalid ++;

    /* ---------------- 处理dup ack计算 ---------------- */

    // 过滤控制报文，不做dup ack流表更新
    if(tcpflag & (TCP_FLAG_SYN | TCP_FLAG_FIN | TCP_FLAG_RST))
        return;

    // 查找流表记录，准备更新状态
    pkt_flow_5_t flow_key = {};
    flow_key.dst_ip = ntohl(ipv4_hdr->dst_addr);
    flow_key.src_ip = ntohl(ipv4_hdr->src_addr);
    flow_key.dst_port = ntohs(tcp_hdr->dst_port);
    flow_key.src_port = ntohs(tcp_hdr->src_port);
    flow_key.protocol = TCP;
    tcp_ack_flow_entry_t *entry = tcp_ack_flow_entry_get_or_create(&flow_key);
    pfm_ensure_ret_void(entry);

    if(entry->state.is_initialized)
    {
        // 判断是否DUP ACK：当前无负载+上次无负载+SEQ相同+ACK相同+Window相同
        if((0 == tcp_payload_size) && 
            (entry->state.last_payload_len == 0) && 
            (ntohl(tcp_hdr->seq_num) == entry->state.last_seq) &&
            (ntohl(tcp_hdr->ack_num) == entry->state.last_ack) &&
            (ntohs(tcp_hdr->window) == entry->state.last_window)
        )
        {
            tds->dup_ack ++;
        }
    }
    // 更新状态
    entry->state.last_ack = ntohl(tcp_hdr->ack_num);
    entry->state.last_seq = ntohl(tcp_hdr->seq_num);
    entry->state.last_window = ntohs(tcp_hdr->window);
    entry->state.last_payload_len = tcp_payload_size;
    entry->state.is_initialized = 1;
    // 老化时间使用low res time即可
    uint64_t cur = low_res_cur_time_get();
    entry->state.last_seen = cur;

    // 处理一定数量的ack报文，触发一次ack流表老化
    ++ g_tcp_dissect_cnt;
    if(unlikely(0 == (g_tcp_dissect_cnt % tcp_ack_flow_entry_AGING_BLCOK)))
        _tcp_flow_aging(cur);
}

void tcp_dissect_init()
{
    tcp_ack_flow_aging_entry_list_init(&g_tcp_aging_flow_list);
    tcp_ack_flow_entry_hash_init(&g_tcp_flow_hashtable);        // init flow hashtable
}

void tcp_dissect_exit()
{
    // TODO: free all flow entry
    tcp_ack_flow_entry_hash_fini(&g_tcp_flow_hashtable);        // fini hashtable
    tcp_ack_flow_aging_entry_list_fini(&g_tcp_aging_flow_list);
}