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
#include "mp/mp_slab.h"

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

// ARP Scan发包间隔，2ms, 500pps
#define ARP_SCAN_SEND_INTERVAL      (2)
// ARP Scan等待
#define ARP_SCAN_WAIT_TIME_SEC      (2)

// ARP cache table 打印表头
#define ARP_REPLY_CACHE_FMT_HEAD    "%-18s%-32s\n"
// ARP reply cache 打印格式，ip mac agingtime
#define ARP_REPLY_CACHE_FMT         "%-16s%-20s%-16s\n"
// ARP reply cache 数据打印格式
#define ARP_REPLY_CACHE_FMT_DATA    "%-16s%-20s%-16.3f\n"

// ARP扫描结果打印格式
#define ARP_SCAN_FMT                "%-16s%-20s\n"

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// arp reply缓存哈希表
static arp_reply_cache_hash_head_t g_arp_reply_cache = {};
// 读写锁，保护cache hash
static ev_rwlock_t g_arp_reply_cache_rwlock = {};

// 声明发包内存池
declare_mem_type_slab_extern(ftx)

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
 * @param[in]   buf     - packet buffer
 * @param[in]   if_name - interface name
 * @param[in]   dip     - dst ip, net order
 * 
 * @retval      error code
 * 
 * @note        构建arp请求报文
 */
static error_code_e _arp_request_build(char *buf, const char *if_name, struct in_addr *dip)
{
    struct in_addr addr = {};
    uint8_t if_mac[MAC_ADDR_SIZE] = {};

    if(ERR_NO_ERROR != ftx_mac_get(if_name, if_mac))
        return ERR_NT_IF_ERROR;

    uint32_t if_ip = 0;
    if(ERR_NO_ERROR != ftx_ip_get(if_name, &if_ip))
        return ERR_NT_IF_ERROR;

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

    return ERR_NO_ERROR;
}

/**
 * @brief       cli hook for arp probe
 * 
 * @param[in]   target_ip
 */
static void* _arp_probe_cli_hook(unsigned char argc, char *argv[]);

static void _arp_zcap_filter_cb(char *packet, int size, void *args);

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static void* _arp_probe_cli_hook(unsigned char argc, char *argv[])
{
    uint8_t *buf = NULL;

    struct in_addr dst = {};
    const char *if_name = argv[1];
    const char *dip_str = argv[0];

    // 检查获取目的ip，网络字节序
    if(1 != inet_pton(AF_INET, dip_str, &dst))
    {
        safe_printf("-arp: Bad Target IP: %s\n", dip_str);
        return NULL;
    }

    buf = mp_slab_node_get(ftx, ARP_STANDER_SIZE);
    if(!buf)
    {
        safe_printf("-arp: System no memory\n");
        return NULL;
    }
    memset(buf, 0, ARP_STANDER_SIZE);

    if(ERR_NO_ERROR == _arp_request_build(buf, if_name, &dst))
    {
        ftx_send(if_name, buf, ARP_STANDER_SIZE);
        safe_printf("-arp: Send Arp Request OK... Wait %ds\n", ARP_PROBE_WAIT_INTERVAL);
    }
    else
    {
        mp_slab_node_put(buf);      // 释放内存
        safe_printf("-arp: Interface error: %s\n", if_name);
        return NULL;
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

static void* _arp_scan_cli_hook(unsigned char argc, char *argv[])
{
    const char *cidr_addr = argv[0];
    const char *if_name = argv[1];
    uint32_t dip_net = 0;
    uint8_t prefix = 0;

    // 检查CIDR地址是否合法，解析ip和prefix
    if(ERR_NO_ERROR != cidr_parse(cidr_addr, &dip_net, &prefix))
    {
        safe_printf("-arp: Bad CIDR address range: %s\n", argv[0]);
        return NULL;
    }
    if(prefix == 31 || prefix == 32)
    {
        safe_printf("-arp: Bad CIDR prefix for arp scan: %d\n", prefix);
        return NULL;
    }
    uint32_t dip = ntohl(dip_net);

    // 检查接口，获取src_mac和sip字段
    uint8_t src_mac[MAC_ADDR_SIZE];
    uint32_t sip_net = 0;
    if(ERR_NO_ERROR != ftx_mac_get(if_name, src_mac) || ERR_NO_ERROR != ftx_ip_get(if_name, &sip_net))
    {
        safe_printf("-arp: Bad Interface: %s\n", if_name);
        return NULL;
    }
    uint32_t sip = htonl(sip_net);

    // 构造arp发送
    char *packet = mp_slab_node_get(ftx, ARP_STANDER_SIZE);
    if(!packet)
    {
        safe_printf("-arp: No avilable memory.\n");
        return NULL;
    }
    memset(packet, 0, ARP_STANDER_SIZE);
    // l2
    pkthdr_l2_t *l2 = (pkthdr_l2_t*)packet;
    memcpy(l2->mac_sa, src_mac, MAC_ADDR_SIZE);
    memset(l2->mac_da, 0xff, MAC_ADDR_SIZE);
    l2->ether_type = htons(ARP);
    // arp
    pkthdr_arp_t *arp = (pkthdr_arp_t*)(packet+sizeof(pkthdr_l2_t));
    arp->hw_type = htons(ARP_HW_TYPE_ETH);
    arp->pro_type = htons(ARP_PRO_TYPE);
    arp->hw_len = ARP_HW_LEN;
    arp->pro_len = ARP_PRO_LEN;
    arp->op_code = htons(ARP_REQUEST);
    memcpy(arp->sender_mac, src_mac, MAC_ADDR_SIZE);
    arp->sender_ip[0] = (sip >> 24) & 0xff;
    arp->sender_ip[1] = (sip >> 16) & 0xff;
    arp->sender_ip[2] = (sip >> 8)  & 0xff;
    arp->sender_ip[3] = (sip >> 0)  & 0xff;
    memset(arp->target_mac, 0, MAC_ADDR_SIZE);

    // 计算范围
    uint32_t mask = (prefix == 0) ? 0U : (~0U << (32 - prefix));
    uint32_t network = dip & mask;          // 网络地址
    uint32_t broadcast = network | ~mask;   // 广播地址
    uint32_t dip_start = network + 1;
    uint32_t dip_end = broadcast - 1;

    safe_printf("-arp: Start Send Arp Rquest...\n");

    for(uint32_t dip = dip_start; dip <= dip_end; ++ dip)
    {
        arp->target_ip[0] = (dip >> 24) & 0xff;
        arp->target_ip[1] = (dip >> 16) & 0xff;
        arp->target_ip[2] = (dip >> 8)  & 0xff;
        arp->target_ip[3] = (dip >> 0)  & 0xff;
        ftx_send(if_name, packet, ARP_STANDER_SIZE);
        // 限制速率，休眠1ms
        usleep(ARP_SCAN_SEND_INTERVAL * 1000);
    }

    safe_printf("-arp: Send Arp Request OK, wait %d s for replying...\n", ARP_SCAN_WAIT_TIME_SEC);
    sleep(ARP_SCAN_SEND_INTERVAL);

    // 收集arp cache，打印结果
    safe_printf("\n" ARP_SCAN_FMT, "ipv4 addr", "mac addr");
    safe_printf(     ARP_SCAN_FMT, "---------", "--------");
    arp_reply_cache_t *cache = NULL;
    for(uint32_t dip = dip_start; dip <= dip_end; ++ dip)
    {
        arp_reply_cache_t key = {.ip = dip};
        ev_with_rdlock(&g_arp_reply_cache_rwlock)
            cache = arp_reply_cache_hash_find(&g_arp_reply_cache, &key);
        if(cache)
        {
            char ip_str[INET_ADDRSTRLEN] = {};
            char mac_str[18] = {};
            uint32_t ip_net = htonl(cache->ip);
            inet_ntop(AF_INET, &ip_net, ip_str, sizeof(ip_str));
            snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x", 
                cache->mac[0], cache->mac[1], cache->mac[2], cache->mac[3], cache->mac[4], cache->mac[5]);
            safe_printf(ARP_SCAN_FMT, ip_str, mac_str);
        }
    }
    safe_printf("\n");

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
    safe_printf("\n");
    safe_printf(ARP_REPLY_CACHE_FMT_HEAD, "", "Arp Cache Table");
    safe_printf("%s", "--------------------------------------------------\n");
    safe_printf(ARP_REPLY_CACHE_FMT, "ipv4 address", "mac address", "aging time(s)");
    safe_printf(ARP_REPLY_CACHE_FMT, "------------", "-----------", "-------------");

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
    safe_printf("%s", "--------------------------------------------------\n\n");

    return NULL;
}

error_code_e arp_cache_query(uint32_t ip, uint8_t *mac)
{
    assert(mac);

    // 在哈希表中查找
    arp_reply_cache_t key = {.ip = ip};
    arp_reply_cache_t *cache = NULL;
    ev_with_rdlock(&g_arp_reply_cache_rwlock)
        cache = arp_reply_cache_hash_find(&g_arp_reply_cache, &key);
    
    if(!cache)
        return ERR_NT_ARP_CACHE_NOT_EXIST;

    memcpy(mac, cache->mac, MAC_ADDR_SIZE);
    return ERR_NO_ERROR;
}

error_code_e arp_request_send(const char *if_name, uint32_t dst_ip)
{
    uint8_t *buffer = (uint8_t*)mp_slab_node_get(ftx, ARP_STANDER_SIZE);
    if(!buffer)
        return ERR_NO_MEM;
    memset(buffer, 0, ARP_STANDER_SIZE);

    struct in_addr addr = {.s_addr = htonl(dst_ip)};

    if(ERR_NO_ERROR != _arp_request_build(buffer, if_name, &addr))
    {
        mp_slab_node_put(buffer);
        return ERR_NT_ARP_SEND_FAIL;
    }

    ftx_send(if_name, buffer, ARP_STANDER_SIZE);
    return ERR_NO_ERROR;
}

void arp_module_init(void)
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
    // 注册cli，进行arp扫描
    cli_param_t param1[] = {
        {.required = 1, .type = PARAM_POS, .help = "CIDR format: ip/mask, e.g. 192.168.0.1/24"},
        {.required = 1, .type = PARAM_VALUE, .short_name = 'i', .help = "interface name"},
    };
    cli_register("arp scan", "arp scan", param1, _arp_scan_cli_hook);
    cli_register("show arp cache", "dump arp cache", NULL, _arp_show_cache_cli_hook);

    // 注册arp抓取
    zcap_register_pkt_filter("eth0", "arp", _arp_zcap_filter_cb, NULL);
    zcap_register_field_len_type("eth0", "arp", 0x0806, 0xffff);

    zcap_register_pkt_filter("wlan0", "arp", _arp_zcap_filter_cb, NULL);
    zcap_register_field_len_type("wlan0", "arp", 0x0806, 0xffff);

    dbg_major("arp module init ok");
}