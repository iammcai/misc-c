/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    ping.c
 * @brief   ping实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-07-16
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-07-16 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <math.h>
#include "net.h"
#include "nt/ping.h"
#include "nt/arp.h"
#include "cli/cli.h"
#include "ftx/ftx.h"
#include "zcap/zcap.h"
#include "event/ev_timer.h"
#include "mp/mp_slab.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// icmp header size
#define PKT_HDR_ICMP_SIZE   (8)
// icmp payload size，携带64B的信息
#define ICMP_PAYLOAD_SIZE   (64)

// icmp请求类型
#define ICMP_TYPE_ECHO_REQUEST  (0x8)
// icmp请求代码
#define ICMP_CODE_ECHO_REQUEST  (0x0)
// icmp回复类型
#define ICMP_TYPE_ECHO_REPLY    (0x0)
// icmp id
#define ICMP_ID                 (0x7903)
// icmp的三层协议类型
#define ICMP                    (0x1)

// ICMP请求次数
#define ICMP_REQUEST_COUNT      (4)

// ARP探测后重试间隔
#define PING_ARP_REQUEST_RETRY_INTERVAL     (200*1000)
// ARP探测最大尝试次数
#define PING_ARP_REQUEST_RETRY_TIMES        (10)

// ping轮询会话的间隔，100ms
#define PING_SESSION_WAIT_INTERVAL      (100)
// ping轮询会话的最大次数，20次，一共2s
#define PING_SESSION_WAIT_TIMES         (20)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// icmp头部定义
typedef struct{
    uint8_t type;       // icmp消息类型
    uint8_t code;       // icmp代码
    uint16_t checksum;  // 校验和
    uint16_t id;        // 标识符，匹配请求和响应
    uint16_t sequence;  // 序列号
}attr_packed pkthdr_icmp_t;
_Static_assert(sizeof(pkthdr_icmp_t) == PKT_HDR_ICMP_SIZE, "sizeof pkthdr_icmp_t must be 8");

// icmp会话状态枚举 
typedef enum{
    ICMP_SESSION_IDLE = 0,      // 空闲
    ICMP_SESSION_WAITING,       // 等待回复
    ICMP_SESSION_REPLY,         // 收到回复
}icmp_seesion_state_e;

// icmp会话管理结构，存放在全局哈希中
typedef struct{
    uint16_t id;    // icmp id，host order
    uint16_t seq;   // icmp seq, host order
    double send_time;           // 发送时间
    ATOMIC_UINT8_T state;       // 会话状态

    uint8_t payload_size;    // 响应负载长度
    uint8_t ttl;         // ttl
    double rtt_ms;              // 往返时间 ms
}icmp_session_t;

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 全局会话，唯一一个
static icmp_session_t g_icmp_session = {};

// 声明发包内存池
declare_mem_type_slab_extern(ftx)

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       get dst_mac by ping dst ip
 * 
 * @param[in]   if_name     - interface name
 * @param[in]   dst_ip      - dip, host order
 * @param[out]  dst_mac     - dmac
 * 
 * @retval      error code
 */
static error_code_e _ping_get_dst_mac(const char *if_name, uint32_t dst_ip, uint8_t *dst_mac)
{
    // 先检查arp缓存中是否存在dst_ip_host，获取dst_mac
    // 失败的话就发送arp请求，等待回应
    if(ERR_NO_ERROR != arp_cache_query(dst_ip, dst_mac))
    {
        if(ERR_NO_ERROR != arp_request_send(if_name, dst_ip))
        {
            dbg_error("send arp request fail");
            return ERR_NT_IF_ERROR;
        }
        // 等待表项生成
        int retry = 0;
        while(retry < PING_ARP_REQUEST_RETRY_TIMES && ERR_NO_ERROR != arp_cache_query(dst_ip, dst_mac))
        {
            usleep(PING_ARP_REQUEST_RETRY_INTERVAL);
            ++ retry;
        }
        if(retry == PING_ARP_REQUEST_RETRY_TIMES)
        {
            safe_printf("-ping: arp query fail\n");
            return ERR_NT_PING_DMAC_UNKNOWN;
        }
    }

    return ERR_NO_ERROR;
}

/**
 * @brief       build icmp echo request packet
 * 
 * @param[in]   packet      - packet buffer
 * @param[in]   dmac        - dest mac
 * @param[in]   smac        - source mac
 * @param[in]   dip         - dest ip, host order
 * @param[in]   sip         - source ip, host order
 * @param[in]   icmp_seq    - icmp sequence
 * 
 * @retval      error code
 * 
 * @note        构建icmp echo request报文
 */
static error_code_e _ping_icmp_echo_request_build(uint8_t *packet, uint8_t *dmac, uint8_t *smac, uint32_t dip, uint32_t sip, uint16_t icmp_seq)
{
    if(!packet)
        return ERR_BAD_PARAM;

    // 构建l2
    pkthdr_l2_t *l2 = (pkthdr_l2_t*)packet;
    memcpy(l2->mac_da, dmac, MAC_ADDR_SIZE);
    memcpy(l2->mac_sa, smac, MAC_ADDR_SIZE);
    l2->ether_type = htons(IPV4);

    // ipv4构建
    pkthdr_ipv4_t *ipv4_hdr = (pkthdr_ipv4_t*)(packet + sizeof(pkthdr_l2_t));
    ipv4_hdr->v_ihl = (0x4 << 4) | (0x5);           // 5*4=20B，以及版本为4
    ipv4_hdr->tos = 0;                              // 优先级为0
    ipv4_hdr->tot_len = htons(PKT_HDR_IPV4_SIZE + PKT_HDR_ICMP_SIZE + ICMP_PAYLOAD_SIZE);   // 总长度固定
    ipv4_hdr->id = htons(ipv4_id_get());            // id全局递增计数器
    ipv4_hdr->flag_frag = htons(0x4000);            // DF=1, MF=0, Offset=0，不分片
    ipv4_hdr->ttl = 64;
    ipv4_hdr->protocol = ICMP;
    ipv4_hdr->checksum = 0;     // 先置0
    ipv4_hdr->src_addr = htonl(sip);
    ipv4_hdr->dst_addr = htonl(dip);
    ipv4_hdr->checksum = calc_checksum(ipv4_hdr, PKT_HDR_IPV4_SIZE);    // 计算校验和

    // icmp构建
    pkthdr_icmp_t *icmp_hdr = (pkthdr_icmp_t*)(packet + sizeof(pkthdr_l2_t) + sizeof(pkthdr_ipv4_t));
    icmp_hdr->type = ICMP_TYPE_ECHO_REQUEST;
    icmp_hdr->code = ICMP_CODE_ECHO_REQUEST;
    icmp_hdr->checksum = 0;     // 计算前先置0
    icmp_hdr->id = htons(ICMP_ID);      // 进程唯一，这里简化了
    icmp_hdr->sequence = htons(icmp_seq);   // 单个会话内匹配，调用方指定
    // 负载填充64B
    uint8_t *icmp_pl = (uint8_t*)icmp_hdr + sizeof(pkthdr_icmp_t);
    memset(icmp_pl, 0, ICMP_PAYLOAD_SIZE);
    snprintf(icmp_pl, ICMP_PAYLOAD_SIZE, "Hello, this is misc-c ping, i am cai");
    // 计算校验和
    icmp_hdr->checksum = calc_checksum(icmp_hdr, PKT_HDR_ICMP_SIZE + ICMP_PAYLOAD_SIZE);

    return ERR_NO_ERROR;
}

/**
 * @brief       cli hook for ping
 * 
 * @param[in]   interface   - ftx interface
 */
static void* _ping_cli_hook(unsigned char argc, char *argv[]);

/**
 * @brief       hook for icmp packet filter
 * 
 * @note        抓到icmp报文后的回调
 */
static void _ping_icmp_filter_cb(char *packet, int len, void *args);

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static void* _ping_cli_hook(unsigned char argc, char *argv[])
{
    // 获取ip和接口
    struct in_addr dst = {};
    uint32_t dst_ip_host = 0;
    char *if_name = argv[1];
    char *dip_str = argv[0];
    uint8_t dst_mac[MAC_ADDR_SIZE] = {};
    // 申请发包内存
    uint8_t *packet = NULL;
    int request_cnt = ICMP_REQUEST_COUNT;

    // 获取ping次数
    if(argc == 3)
    {
        request_cnt = cli_param_parse_str_2_u32(argv[2]);
        if(request_cnt <= 0)
        {
            safe_printf("-ping: Bad param for -c: %s", argv[2]);
            return NULL;
        }
    }

    // 检查获取目的ip，网络字节序
    if(1 != inet_pton(AF_INET, dip_str, &dst))
    {
        safe_printf("-ping: Bad Target IP: %s\n", dip_str);
        return NULL;
    }
    dst_ip_host = ntohl(dst.s_addr);        // 主机序DIP

    // 检查发包接口
    uint8_t if_mac[MAC_ADDR_SIZE] = {};
    if(ERR_NO_ERROR != ftx_mac_get(if_name, if_mac))
    {
        safe_printf("-ping: No mac for Interface Name: %s\n", if_name);
        return NULL;
    }
    uint32_t if_ip = 0;
    if(ERR_NO_ERROR != ftx_ip_get(if_name, &if_ip))
    {
        safe_printf("-ping: No ip for Interface Name: %s\n", if_name);
        return NULL;
    }
    uint32_t if_ip_host = ntohl(if_ip);

    // 获取目的mac
    if(ERR_NO_ERROR != _ping_get_dst_mac(if_name, dst_ip_host, dst_mac))
    {
        safe_printf("-ping: Unknown mac for ip %s\n", dip_str);
        return NULL;
    }

    int seq = 0;
    safe_printf("\nPING %s %d bytes of data.\n"
        "Note: RTT includes internal processing overhead, for connectivity check only.\n",
        dip_str, ICMP_PAYLOAD_SIZE
    );
    int transmit = 0, received = 0, start_time = mono_time_get();
    double *rtt_arr = (double*)mp_calloc(request_cnt, sizeof(double));
    double rtt_max = 0, rtt_min = PING_SESSION_WAIT_INTERVAL * PING_SESSION_WAIT_TIMES, rtt_avg = 0, rtt_mdev = 0, rtt_sum = 0; // rtt统计
    for(; seq < request_cnt; ++ seq)
    {
        // 构造icmp报文发送
        int icmp_len = PKT_HDR_L2_SIZE + PKT_HDR_IPV4_SIZE + PKT_HDR_ICMP_SIZE + ICMP_PAYLOAD_SIZE;
        packet = (uint8_t*)mp_slab_node_get(ftx, icmp_len);
        if(!packet)
        {
            safe_printf("-ping: System no memory.\n");
            break;
        }
        memset(packet, 0, icmp_len);

        if(ERR_NO_ERROR != _ping_icmp_echo_request_build(packet, dst_mac, if_mac, dst_ip_host, if_ip_host, seq))
        {
            mp_slab_node_put(packet);
            safe_printf("-ping: Icmp echo-request packet build fail.\n");
            break;
        }

        // 设置会话
        g_icmp_session.seq = seq;
        g_icmp_session.send_time = mono_time_get();
        ATOM_STORE(&g_icmp_session.state, ICMP_SESSION_WAITING, MORDER_RELEASE);

        // 将报文发送出去
        ftx_send(if_name, packet, icmp_len);
        transmit ++;

        // 等待最多2s的超时
        int retry = 0;
        while(retry < PING_SESSION_WAIT_TIMES && ATOM_LOAD(&g_icmp_session.state, MORDER_ACQUIRE) != ICMP_SESSION_REPLY)
        {
            usleep(PING_SESSION_WAIT_INTERVAL * 1000);      // sleep 100ms
            ++ retry;
        }
        if(retry == PING_SESSION_WAIT_TIMES)
            safe_printf("Destination Unreachable\n");
        else
        {
            received ++;
            safe_printf("%d bytes from %s: icmp_seq=%d ttl=%d time=%.3f ms\n",
                g_icmp_session.payload_size, dip_str, g_icmp_session.seq, g_icmp_session.ttl, g_icmp_session.rtt_ms
            );
            rtt_arr[seq] = g_icmp_session.rtt_ms;
            rtt_max = rtt_max > g_icmp_session.rtt_ms ? rtt_max : g_icmp_session.rtt_ms;
            rtt_min = rtt_min < g_icmp_session.rtt_ms ? rtt_min : g_icmp_session.rtt_ms;
            rtt_sum += rtt_arr[seq];
        }
        // sleep 1s，符合用户使用习惯
        if(seq != request_cnt - 1)
            sleep(1);
    }
    int end_time = mono_time_get();

    // 计算数据
    safe_printf("--- %s ping statistics ---\n", dip_str);
    safe_printf("%d packets transmitted, %d received, %.0f%% packet loss, time %dms\n",
        transmit, received, (double)(transmit-received)/transmit, end_time - start_time);
    // 计算平均值
    rtt_avg = rtt_sum / received;
    // 计算mdev
    double rtt_diff_sum = 0;    // 平方差之和
    for(int i = 0; i < request_cnt; ++ i)
        if(rtt_arr[i] == 0)
            continue;
        else
            rtt_diff_sum += ((rtt_arr[i] - rtt_avg)*(rtt_arr[i] - rtt_avg));
    double rtt_variance = rtt_diff_sum / received;
    rtt_mdev = sqrt(rtt_variance);
    // 打印数据
    safe_printf("rtt min/avg/max/mdev = %.3f/%.3f/%.3f/%.3f ms\n\n", rtt_min, rtt_avg, rtt_max, rtt_mdev);

    return NULL;
}

static void _ping_icmp_filter_cb(char *packet, int len, void *args)
{
    // 仅处理reply
    pkthdr_icmp_t *icmp_hdr = (pkthdr_icmp_t*)(packet + sizeof(pkthdr_l2_t) + sizeof(pkthdr_ipv4_t));
    if(icmp_hdr->type != ICMP_TYPE_ECHO_REPLY)
        return;

    // 检查会话状态进行修改
    if(ATOM_LOAD(&g_icmp_session.state, MORDER_ACQUIRE) != ICMP_SESSION_WAITING)
        return;

    // CAS，确保只有一个线程能修改会话，防止收到重复icmp包
    char expected = ICMP_SESSION_WAITING;
    if(ATOM_CMP_XCHG_WEAK(&g_icmp_session.state, &expected, ICMP_SESSION_REPLY, MORDER_SEQ_SCT, MORDER_RELAXED))
    {
        // 检查id和seq是否匹配
        if(ntohs(icmp_hdr->id) != g_icmp_session.id || ntohs(icmp_hdr->sequence) != g_icmp_session.seq)
            return;

        // 更新数据
        g_icmp_session.rtt_ms = mono_time_get() - g_icmp_session.send_time;
        pkthdr_ipv4_t *ipv4_hdr = (pkthdr_ipv4_t*)(packet + sizeof(pkthdr_l2_t));
        g_icmp_session.payload_size = ntohs(ipv4_hdr->tot_len) - (ipv4_hdr->v_ihl & 0xf) * 4 - PKT_HDR_ICMP_SIZE;    // 计算icmp payload长度
        g_icmp_session.ttl = ipv4_hdr->ttl;
    }
}

void ping_module_init()
{
    // 初始化会话
    memset(&g_icmp_session, 0, sizeof(icmp_session_t));
    g_icmp_session.id = ICMP_ID;
    ATOM_STORE(&g_icmp_session.state, ICMP_SESSION_IDLE, MORDER_RELAXED);   // 设置idle

    // 注册cli支持ping
    cli_param_t param0[] = {
        {.required = 1, .type = PARAM_POS, .help = "target ip"},
        {.required = 1, .short_name = 'i', .type = PARAM_VALUE, .help = "interface"},
        {.required = 0, .short_name = 'c', .type = PARAM_VALUE, .help = "ping times"},
    };
    cli_register("ping", "ping", param0, _ping_cli_hook);

    // 注册报文过滤条件icmp
    zcap_register_pkt_filter("eth0", "icmp", _ping_icmp_filter_cb, NULL);
    zcap_register_field_len_type("eth0", "icmp", 0x0800, 0xffff);
    zcap_register_field_pro_type("eth0", "icmp", 0x1, 0xff);

    zcap_register_pkt_filter("wlan0", "icmp", _ping_icmp_filter_cb, NULL);
    zcap_register_field_len_type("wlan0", "icmp", 0x0800, 0xffff);
    zcap_register_field_pro_type("wlan0", "icmp", 0x1, 0xff);

    dbg_major("ping module init ok");
}