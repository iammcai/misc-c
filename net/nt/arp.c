/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    arp.c
 * @brief   arp相关功能实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-07-10
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-07-10 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "nt/arp.h"
#include "ftx/ftx.h"
#include "cli/cli.h"
#include "zcap/zcap.h"
#include "type/type_hash.h"
#include "event/ev_lock.h"
#include "event/ev_timer.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// arp结构
typedef struct{
    uint16_t hw_type;
    uint16_t pro_type;
    uint8_t hw_len;
    uint8_t pro_len;
    uint16_t op_code;
    uint8_t sender_mac[MAC_ADDR_SIZE];
    uint8_t sender_ip[IPV4_ADDR_SIZE];
    uint8_t target_mac[MAC_ADDR_SIZE];
    uint8_t target_ip[IPV4_ADDR_SIZE];
}attr_packed pkthdr_arp_t;
_Static_assert(sizeof(pkthdr_arp_t) == 28, "sizeof pkthdr_arp_t must be 28");

// arp opcode操作码枚举
typedef enum{
    ARP_REQUEST = 1,    // ARP请求
    ARP_REPLY = 2,      // ARP响应
}arp_code_e;

// 预定义arp reply哈希表类型
pre_declare_hash(arp_reply_cache)

// ARP回复的缓存条目
typedef struct{
    uint32_t ip;                // 作为key
    uint8_t mac[MAC_ADDR_SIZE];
    ev_timer_t *timer;          // 定时器，到期清除该条目
    arp_reply_cache_hash_item_t item;   // hash item
}arp_reply_cache_t;

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 标准arp报文长度
#define ARP_STANDER_SIZE        (42)
// 硬件类型 1-ethernet
#define ARP_HW_TYPE_ETH         (1)
// 软件协议类型 ipv4
#define ARP_PRO_TYPE            IPV4
// ARP硬件地址长度，6
#define ARP_HW_LEN              (6)
// ARP软件地址长度，ipv3对应4
#define ARP_PRO_LEN             (4)

// ARP reply cache老化时间
#define ARP_REPLY_CACHE_AGING_TIME      (300 * 1000)    // 300s

// ARP Probe等待时长，单位s
#define ARP_PROBE_WAIT_INTERVAL     (1)

// ARP reply cache 打印格式，ip mac agingtime
#define ARP_REPLY_CACHE_FMT         "%-16s%-20s%-16s\n"
// ARP reply cache 数据打印格式
#define ARP_REPLY_CACHE_FMT_DATA    "%-16s%-20s%-16.3f\n"

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// arp reply缓存哈希表
static arp_reply_cache_hash_head_t g_arp_reply_cache = {};
// 读写锁，保护cache hash
static ev_rwlock_t g_arp_reply_cache_rwlock = {};

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       cmp two arp reply cache
 * 
 * @param[in]   c1  - cache 1
 * @param[in]   c2  - cache 2
 * 
 * @retval      cmp val
 * 
 * @note        cmp by ip
 */
static attr_pure_inline int _arp_reply_cache_cmp(arp_reply_cache_t *c1, arp_reply_cache_t *c2)
{
    assert(c1 && c2);
    return c1->ip - c2->ip;
}

/**
 * @brief       hash arp reply cache
 * 
 * @param[in]   c   - cache
 * 
 * @retval      hash val
 * 
 * @note        hash by ip
 */
static attr_pure_inline unsigned int _arp_reply_cache_hash(arp_reply_cache_t *c)
{
    assert(c);
    return type_hash_jhash_32bit(c->ip, 0);
}

// 定义arp reply cache hash操作
declare_hash(arp_reply_cache, arp_reply_cache, arp_reply_cache_t, item, 1, 31, _arp_reply_cache_cmp, _arp_reply_cache_hash)

/**
 * @brief       build arp request packet
 * 
 * @note        构建arp请求报文
 */
static int _arp_request_build(char *buf, char *if_name, struct in_addr *dip)
{
    struct in_addr addr = {};
    uint8_t if_mac[MAC_ADDR_SIZE] = {};

    if(0 != ftx_mac_get(if_name, if_mac))
    {
        safe_printf("-arp: No mac for Interface Name: %s\n", if_name);
        return -1;
    }
    uint32_t if_ip = 0;
    if(0 != ftx_ip_get(if_name, &if_ip))
    {
        safe_printf("-arp: No ip for Interface Name: %s\n", if_name);
        return -1;
    }

    // 构造L2内容
    pkthdr_l2_t *l2 = (pkthdr_l2_t*)buf;
    memset(l2->mac_da, 0xff, MAC_ADDR_SIZE);    // bc
    memcpy(l2->mac_sa, if_mac, MAC_ADDR_SIZE);
    l2->ether_type = htons(ARP);
    // 构造arp内容
    pkthdr_arp_t *arp = (pkthdr_arp_t*)(buf + sizeof(pkthdr_l2_t));
    arp->hw_type = htons(ARP_HW_TYPE_ETH);
    arp->pro_type = htons(ARP_PRO_TYPE);
    arp->hw_len = ARP_HW_LEN;
    arp->pro_len = ARP_PRO_LEN;
    arp->op_code = htons(ARP_REQUEST);
    memcpy(arp->sender_mac, if_mac, MAC_ADDR_SIZE);
    uint32_t sip_host = ntohl(if_ip), dip_host = ntohl(dip->s_addr);
    arp->sender_ip[0] = (uint8_t)(sip_host >> 24);
    arp->sender_ip[1] = (uint8_t)(sip_host >> 16);
    arp->sender_ip[2] = (uint8_t)(sip_host >> 8);
    arp->sender_ip[3] = (uint8_t)(sip_host >> 0);
    memset(arp->target_mac, 0, MAC_ADDR_SIZE);
    arp->target_ip[0] = (uint8_t)(dip_host >> 24);
    arp->target_ip[1] = (uint8_t)(dip_host >> 16);
    arp->target_ip[2] = (uint8_t)(dip_host >> 8);
    arp->target_ip[3] = (uint8_t)(dip_host >> 0);

    return 0;
}

/**
 * @brief       cli hook for arp probe
 * 
 * @param[in]   target_ip
 */
static void* _arp_probe_cli_hook(unsigned char argc, char *argv[]);

static void _arp_zcap_filter_cb(char *packet, int size, void *args);

/**
 * @brief       ctor init arp
 * 
 * @note        初始化arp，优先级必须比MID低，以注册抓包过滤
 */
static void arp_early_init(void) attr_ctor(CTOR_PRIO_LOW);

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static void* _arp_probe_cli_hook(unsigned char argc, char *argv[])
{
    uint8_t buf[ARP_STANDER_SIZE] = {};
    struct in_addr dst = {};
    char *if_name = argv[1];
    char *dip_str = argv[0];

    // 检查获取目的ip，网络字节序
    if(1 != inet_pton(AF_INET, dip_str, &dst))
    {
        safe_printf("-arp: Bad Target IP: %s\n", dip_str);
        return NULL;
    }
    if(0 == _arp_request_build(buf, if_name, &dst))
    {
        ftx_send(if_name, buf, ARP_STANDER_SIZE);
        safe_printf("-arp: Send Arp Request OK... Wait %ds\n", ARP_PROBE_WAIT_INTERVAL);
    }

    // 等待1s
    sleep(ARP_PROBE_WAIT_INTERVAL);

    // 检查ARP Reply Cache
    uint32_t ip_host = ntohl(dst.s_addr);
    arp_reply_cache_t key = {.ip = ip_host};
    arp_reply_cache_t *cache = NULL;
    ev_with_rdlock(&g_arp_reply_cache_rwlock)
        cache = arp_reply_cache_hash_find(&g_arp_reply_cache, &key);

    if(!cache)
        safe_printf("-arp: Arp Probe Fail\n");
    else
        safe_printf("mac address: %02x:%02x:%02x:%02x:%02x:%02x\n",
            cache->mac[0],cache->mac[1],cache->mac[2],cache->mac[3],cache->mac[4],cache->mac[5]);

    return NULL;
}

/**
 * @brief       del arp reply cache from hashtable
 * 
 * @param[in]   cache
 */
static void _arp_reply_cache_del(void *args)
{
    arp_reply_cache_t *cache = (arp_reply_cache_t*)args;

    // 检查cache是否存在
    arp_reply_cache_t key = {.ip = cache->ip};
    cache = arp_reply_cache_hash_find(&g_arp_reply_cache, &key);
    if(!cache)
        return;

    ev_with_wrlock(&g_arp_reply_cache_rwlock)
    {
        // 销毁定时器
        ev_timer_destroy(&cache->timer);
        // 从哈希表中移除
        arp_reply_cache_hash_del(&g_arp_reply_cache, cache);
    }

    dbg("del cache add for ip %08x, mac %02x:%02x:%02x:%02x:%02x:%02x",
                cache->ip, cache->mac[0], cache->mac[1], cache->mac[2], cache->mac[3], cache->mac[4], cache->mac[5]);

    // 归还内存
    mp_free(cache, sizeof(arp_reply_cache_t));
}

/**
 * @brief       add arp reply cache into hashtable
 * 
 * @param[in]   ip  - ip, host order
 * @param[in]   mac - mac
 * 
 * @note        查找对应cache是否存在，进行新增or更新
 */
static void _arp_reply_cache_add(uint32_t ip, uint8_t *mac)
{
    ev_with_wrlock(&g_arp_reply_cache_rwlock)
    {
        arp_reply_cache_t key = {.ip = ip};
        arp_reply_cache_t *cache = arp_reply_cache_hash_find(&g_arp_reply_cache, &key);

        // 没有cache，构造并加入
        if(!cache)
        {
            cache = (arp_reply_cache_t*)mp_calloc(1, sizeof(arp_reply_cache_t));
            cache->ip = ip;
            memcpy(cache->mac, mac, MAC_ADDR_SIZE);
            cache->timer = ev_timer_create(ARP_REPLY_CACHE_AGING_TIME, _arp_reply_cache_del, cache); // 创建老化定时器

            // 加入哈希表，启动老化
            arp_reply_cache_hash_add(&g_arp_reply_cache, cache);
            ev_timer_start(cache->timer);

            dbg("arp cache add for ip %08x, mac %02x:%02x:%02x:%02x:%02x:%02x",
                ip, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
        else
        {
            // 更新MAC
            memcpy(cache->mac, mac, MAC_ADDR_SIZE);

            // 重置老化时间
            ev_timer_destroy(&cache->timer);
            cache->timer = ev_timer_create(ARP_REPLY_CACHE_AGING_TIME, _arp_reply_cache_del, cache);
            ev_timer_start(cache->timer);

            dbg("arp cache update for ip %08x, mac %02x:%02x:%02x:%02x:%02x:%02x",
                ip, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
    }
}

static void _arp_zcap_filter_cb(char *packet, int size, void *args)
{
    // 报文错误
    if(size < ARP_STANDER_SIZE)
    {
        dbg_error("malformat arp");
        return;
    }

    pkthdr_l2_t *l2_hdr = (pkthdr_l2_t*)packet;
    pkthdr_arp_t *arp_hdr = (pkthdr_arp_t*)((uint8_t*)packet + sizeof(pkthdr_l2_t));

    if(ntohs(arp_hdr->op_code) == ARP_REPLY)        // 处理ARP响应
    {
        dbg("arp reply get");
        uint32_t ip = ntohl(*(uint32_t*)arp_hdr->sender_ip);
        _arp_reply_cache_add(ip, arp_hdr->sender_mac);
    }
}

/**
 * @brief       cli hook for show arp cache
 * 
 * @note        打印arp cache
 */
static void* _arp_show_cache_cli_hook(unsigned char argc, char *argv[])
{
    safe_printf( "\n" ARP_REPLY_CACHE_FMT, "ipv4 address", "mac address", "aging time(s)");
    safe_printf(      ARP_REPLY_CACHE_FMT, "------------", "-----------", "-------------");

    char ip_str[INET_ADDRSTRLEN] = {};
    char mac_str[18] = {};

    ev_with_rdlock(&g_arp_reply_cache_rwlock)
    {
        arp_reply_cache_t *cache = arp_reply_cache_hash_first(&g_arp_reply_cache);
        while(cache)
        {
            uint32_t ip_net = htonl(cache->ip);
            inet_ntop(AF_INET, &ip_net, ip_str, sizeof(ip_str));
            snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x", 
                cache->mac[0], cache->mac[1], cache->mac[2], cache->mac[3], cache->mac[4], cache->mac[5]);

            safe_printf(ARP_REPLY_CACHE_FMT_DATA, ip_str, mac_str, (double)ev_timer_remain(cache->timer)/1000);

            cache = arp_reply_cache_hash_next(&g_arp_reply_cache, cache);
        }
    }

    return NULL;
}

static void arp_early_init(void)
{
    // 初始化arp reply cache
    arp_reply_cache_hash_init(&g_arp_reply_cache);
    ev_rwlock_init(&g_arp_reply_cache_rwlock);

    // 注册cli，进行arp探测
    cli_param_t param0[] = {
        {.required = 1, .type = PARAM_VALUE, .short_name = 't', .help = "target ip"},
        {.required = 1, .type = PARAM_VALUE, .short_name = 'i', .help = "interface name"}
    };
    cli_register("arp probe", "send arp probe", param0, _arp_probe_cli_hook);
    cli_register("show arp cache", "dump arp cache", NULL, _arp_show_cache_cli_hook);

    // 注册arp抓取
    zcap_register_pkt_filter("eth0", "arp", _arp_zcap_filter_cb, NULL);
    zcap_register_field_len_type("eth0", "arp", 0x0806, 0xffff);
}