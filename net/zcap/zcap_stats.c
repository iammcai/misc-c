/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    zcap_stats.c
 * @brief   zcap数据统计
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

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <pthread.h>
#include <sys/prctl.h>

#include "zcap/zcap_internal.h"
#include "swdg/swdg.h"
#include "fifo/fifo.h"
#include "event/ev_lock.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 速率计算间隔1s
#define ZCAP_RATE_CAL_INTERVAL          (1000)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

//

/**
 * @brief       tx速率计算线程的工作函数
 */
static void* _zcap_tx_rate_cal_wf(void* args);

/**
 * @brief       rx速率计算线程的工作函数
 */
static void* _zcap_rx_rate_cal_wf(void *args);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 统计接口流量rx速率的线程ID
static pthread_t g_zcap_rx_rate_cal_tid;
// 运行标志
static ATOMIC_UINT8_T g_zcap_rx_rate_cal_runing = 0;

// 统计接口流量tx速率的线程ID
static pthread_t g_zcap_tx_rate_cal_tid;
// 运行标志
static ATOMIC_UINT8_T g_zcap_tx_rate_cal_runing = 0;

// zcaptor hashtable
extern zcaptor_hash_head_t g_zcaptor_hash_head;
// rwlock
extern ev_rwlock_t g_zcaptor_hash_rwlock;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void _zcap_packet_flow_key_extract(zcap_packet_t *packet)
{
    // 检查报文大小是否大于l2长度
    uint32_t len = packet->len;
    if(len < sizeof(struct ethhdr))
    {
        packet->flow_key.err_pkt = 1;
        return;
    }

    const struct ethhdr *eth = (const struct ethhdr*)packet->packet;    // l2头部
    uint16_t ether_type = ntohs(eth->h_proto);
    uint32_t offset = sizeof(struct ethhdr);    // 偏移，用来计算下标是否越界
    zcap_pakcet_flow_key_t *flow_key = &packet->flow_key;

    // 暂时不考虑vlan
    if(ETH_P_IP == ether_type)
    {
        if(offset + sizeof(struct iphdr) > len)     // 检查l3头部是否完整
        {
            packet->flow_key.err_pkt = 1;
            return;
        }
        const struct iphdr *ip = (const struct iphdr*)((uint8_t*)packet->packet + offset);  // l3头部
        uint32_t ip_hdr_len = ip->ihl * 4;      // l3的长度，以4B为单位，包含头部
        if(offset + ip_hdr_len > len)       // 检查l3是否完整
        {
            packet->flow_key.err_pkt = 1;
            return;
        }

        flow_key->src_ip = ip->saddr;
        flow_key->dst_ip = ip->daddr;
        flow_key->protocol = ip->protocol;

        if(IPPROTO_TCP == ip->protocol || IPPROTO_UDP == ip->protocol)  // 处理tcp/udp
        {
            const uint16_t *l4port = (const uint16_t*)((uint8_t*)ip + ip_hdr_len);
            offset += ip_hdr_len;
            if(offset + 4 > len)    // 检查srcport和dstport是否存在
            {
                packet->flow_key.err_pkt = 1;
                return;
            }

            flow_key->src_port = ntohs(l4port[0]);
            flow_key->dst_port = ntohs(l4port[1]);
        }
    }
    else    // 非IPv4报文使用srcMac和etherType作为五元组
    {
        flow_key->protocol = (uint8_t)(ether_type & 0xff);
        memcpy(&flow_key->src_ip, eth->h_source, 4);
    }
}

void _zcap_stats_count_handle(zcap_alz_stats_t *stats, zcap_packet_t *packet)
{
    pfm_ensure_ret_void(stats && packet);
    stats->counts ++;
    stats->bytes += packet->len;
}

void _zcap_stats_size_handle(zcap_alz_stats_t *stats, zcap_packet_t *packet)
{
    pfm_ensure_ret_void(stats && packet);
    int len = packet->len;
    if(len >= 1518)         ++ stats->size_gt_1518;
    else if(len >= 1024)    ++ stats->size_1024_1518;
    else if(len >= 512)     ++ stats->size_512_1024;
    else if(len >= 256)     ++ stats->size_256_512;
    else if(len >= 128)     ++ stats->size_128_256;
    else if(len >= 64)      ++ stats->size_64_128;
    else                    ++ stats->size_lt_64;
}

void _zcap_stats_cast_handle(zcap_alz_stats_t *stats, zcap_packet_t *packet)
{
    pfm_ensure_ret_void(stats && packet);
    pkthdr_l2_t *l2 = (pkthdr_l2_t*)(packet->packet);
    uint8_t bc_mac_addr[MAC_ADDR_SIZE] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    if(0 == memcmp(l2->mac_da, bc_mac_addr, MAC_ADDR_SIZE))
        stats->bc_count ++;
    else if(l2->mac_da[0] & 0x01)
        stats->mc_count ++;
    else
        stats->uc_count ++;
}

void _zcap_stats_protocol_handle(zcap_alz_stats_t *stats, zcap_packet_t *packet)
{
    pfm_ensure_ret_void(stats && packet);

    size_t offset = 0;
    size_t total_size = packet->len;
    char *ctx = packet->packet;

    // l2
    if((offset + PKT_HDR_L2_SIZE) > total_size)
        goto others;
    pkthdr_l2_t *l2_hdr = (pkthdr_l2_t*)ctx;
    uint16_t ethertype = ntohs(l2_hdr->ether_type);
    offset += PKT_HDR_L2_SIZE;
    ctx += PKT_HDR_L2_SIZE;

    if(ethertype == ARP)
    {
        stats->arp ++;
        return;
    }
    else if(ethertype == IPV4)
    {
        // l3
        if(offset + sizeof(pkthdr_ipv4_t) > total_size)
            goto others;
        pkthdr_ipv4_t *ipv4_hdr = (pkthdr_ipv4_t*)ctx;
        uint8_t ihl = IPV4_HEADER_SIZE(ipv4_hdr);       // 计算实际的ipv4 header长度
        offset += ihl;
        ctx += ihl;

        uint8_t protocol = ipv4_hdr->protocol;
        if(protocol == ICMP)
        {
            stats->icmp ++;
            return;
        }
        else if(protocol == TCP)
        {
            if(offset + sizeof(pkthdr_tcp_t) > total_size)
                goto others;
            pkthdr_tcp_t *tcp_hdr = (pkthdr_tcp_t*)ctx;
            uint8_t tcp_hdr_len = TCP_HEADER_SIZE(tcp_hdr);     // 实际tcp header长度
            offset += tcp_hdr_len;
            ctx += tcp_hdr_len;

            tcp_dissect(packet->packet, packet->len, &stats->tcp);

            uint16_t dport = ntohs(tcp_hdr->dst_port);
            if(dport == SSH_TCP_PORT)
            {
                stats->ssh ++;
                return;
            }
            else if(dport == MISC_C_WEB_TCP_PORT)
            {
                stats->miscc_web ++;
                return;
            }
            else
            {
                stats->tcp_other ++;
                return;
            }
        }
        else if(protocol == UDP)
        {
            if(offset + sizeof(pkthdr_udp_t) > total_size)
                goto others;
            pkthdr_udp_t *udp_hdr = (pkthdr_udp_t*)ctx;
            offset += sizeof(pkthdr_udp_t);
            ctx += sizeof(pkthdr_udp_t);

            uint16_t dport = ntohs(udp_hdr->dport);
            if(dport == DNS_UDP_PORT)                                   // dns
            {
                stats->dns ++;
                return;
            }
            else if(dport == DHCP_UDP_CLIENT_PORT || dport == DHCP_UDP_SERVER_PORT)     // dhcp
            {
                stats->dhcp ++;
                return;
            }
            else
            {
                stats->udp_other ++;
                return;
            }
        }
        else
            goto others;
    }

others:
    stats->others ++;
    return;
}

static void* _zcap_tx_rate_cal_wf(void* args)
{
    const char *th_name = "tx_rate_cal";
    prctl(PR_SET_NAME, th_name);

    swdg_register(th_name, ZCAP_RATE_CAL_INTERVAL * 3);

    ATOM_STORE(&g_zcap_tx_rate_cal_runing, 1, MORDER_RELAXED);
    while(ATOM_LOAD(&g_zcap_tx_rate_cal_runing, MORDER_RELAXED))
    {
        swdg_feed();

        ev_with_rdlock(&g_zcaptor_hash_rwlock)
        {
            zcap_t *captor = zcaptor_hash_first(&g_zcaptor_hash_head);
            while(captor)
            {
                // 统计当前总数
                uint64_t curr_counts = 0;
                uint64_t curr_bytes = 0;
                for(int q = 0; q < ZCAP_HASH_Q_SIZE; ++ q)
                {
                    curr_counts += captor->alz[q].tx_stats.counts;
                    curr_bytes += captor->alz[q].tx_stats.bytes;
                }
                // 计算上次差值，更新
                ev_with_wrlock(&captor->stats[ZCAP_PACKET_EGRESS].lock)
                {
                    uint64_t delta_counts = curr_counts - captor->stats[ZCAP_PACKET_EGRESS].last_counts;
                    uint64_t delta_bytes = curr_bytes - captor->stats[ZCAP_PACKET_EGRESS].last_bytes;
                    captor->stats[ZCAP_PACKET_EGRESS].last_counts = curr_counts;
                    captor->stats[ZCAP_PACKET_EGRESS].last_bytes = curr_bytes;
                    captor->stats[ZCAP_PACKET_EGRESS].pps = (double)delta_counts / (ZCAP_RATE_CAL_INTERVAL / 1000.0);
                    captor->stats[ZCAP_PACKET_EGRESS].Bps = (double)delta_bytes / (ZCAP_RATE_CAL_INTERVAL / 1000.0);
                    // 更新tx history
                    fifo_t *tx_history = captor->stats[ZCAP_PACKET_EGRESS].Bps_history;
                    double tmp;
                    if(fifo_is_full(tx_history))
                        fifo_read(tx_history, (char*)&tmp, sizeof(double), 1);
                    fifo_write(tx_history, &captor->stats[ZCAP_PACKET_EGRESS].Bps, sizeof(double));
                }

                captor = zcaptor_hash_next(&g_zcaptor_hash_head, captor);
            }
        }

        usleep(ZCAP_RATE_CAL_INTERVAL * 1000);
    }

    swdg_deregister(th_name);

    return NULL;
}

static void* _zcap_rx_rate_cal_wf(void* args)
{
    const char *th_name = "rx_rate_cal";
    prctl(PR_SET_NAME, th_name);

    swdg_register(th_name, ZCAP_RATE_CAL_INTERVAL * 3);

    ATOM_STORE(&g_zcap_rx_rate_cal_runing, 1, MORDER_RELAXED);
    while(ATOM_LOAD(&g_zcap_rx_rate_cal_runing, MORDER_RELAXED))
    {
        swdg_feed();

        ev_with_rdlock(&g_zcaptor_hash_rwlock)
        {
            zcap_t *captor = zcaptor_hash_first(&g_zcaptor_hash_head);
            while(captor)
            {
                // 统计当前总数
                uint64_t curr_counts = 0;
                uint64_t curr_bytes = 0;
                for(int q = 0; q < ZCAP_HASH_Q_SIZE; ++ q)
                {
                    curr_counts += captor->alz[q].rx_stats.counts;
                    curr_bytes += captor->alz[q].rx_stats.bytes;
                }
                // 计算上次差值，更新
                ev_with_wrlock(&captor->stats[ZCAP_PACKET_INGRESS].lock)
                {
                    uint64_t delta_counts = curr_counts - captor->stats[ZCAP_PACKET_INGRESS].last_counts;
                    uint64_t delta_bytes = curr_bytes - captor->stats[ZCAP_PACKET_INGRESS].last_bytes;
                    captor->stats[ZCAP_PACKET_INGRESS].last_counts = curr_counts;
                    captor->stats[ZCAP_PACKET_INGRESS].last_bytes = curr_bytes;
                    captor->stats[ZCAP_PACKET_INGRESS].pps = (double)delta_counts / (ZCAP_RATE_CAL_INTERVAL / 1000.0);
                    captor->stats[ZCAP_PACKET_INGRESS].Bps = (double)delta_bytes / (ZCAP_RATE_CAL_INTERVAL / 1000.0);
                    // 更新rx history
                    fifo_t *rx_history = captor->stats[ZCAP_PACKET_INGRESS].Bps_history;
                    double tmp;
                    if(fifo_is_full(rx_history))
                        fifo_read(rx_history, (char*)&tmp, sizeof(double), 1);
                    fifo_write(rx_history, &captor->stats[ZCAP_PACKET_INGRESS].Bps, sizeof(double));
                }

                captor = zcaptor_hash_next(&g_zcaptor_hash_head, captor);
            }
        }

        usleep(ZCAP_RATE_CAL_INTERVAL * 1000);
    }

    swdg_deregister(th_name);

    return NULL;
}

void _zcap_stats_src_top_handle(zcap_t *captor, char *src_mac)
{
    pfm_ensure_ret_void(captor && src_mac);

    _msg_q_push(captor->top.msgq, src_mac, MAC_ADDR_SIZE);
}

void _zcap_rx_rate_cal_start()
{
    pthread_create(&g_zcap_rx_rate_cal_tid, NULL, _zcap_rx_rate_cal_wf, NULL);
    while(!ATOM_LOAD(&g_zcap_rx_rate_cal_runing, MORDER_RELAXED))
        usleep(100);
}

void _zcap_rx_rate_cal_stop()
{
    ATOM_STORE(&g_zcap_rx_rate_cal_runing, 0, MORDER_RELAXED);
    pthread_join(g_zcap_rx_rate_cal_tid, NULL);
}

void _zcap_tx_rate_cal_start()
{
    pthread_create(&g_zcap_tx_rate_cal_tid, NULL, _zcap_tx_rate_cal_wf, NULL);
    while(!ATOM_LOAD(&g_zcap_tx_rate_cal_runing, MORDER_RELAXED))
        usleep(100);
}

void _zcap_tx_rate_cal_stop()
{
    ATOM_STORE(&g_zcap_tx_rate_cal_runing, 0, MORDER_RELAXED);
    pthread_join(g_zcap_tx_rate_cal_tid, NULL);
}