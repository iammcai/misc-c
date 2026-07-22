/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    net.h
 * @brief   收发包公用接口头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-07-08
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-07-08 | cai | Initial creation.
 */

#ifndef __NET_H__
#define __NET_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include "plat/compiler.h"
#include "plat/debug.h"
#include "mp/mp.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// MAC地址长度
#define MAC_ADDR_SIZE   (6)
// IPV4地址长度
#define IPV4_ADDR_SIZE  (4)
// L2 Header长度
#define PKT_HDR_L2_SIZE (14)
// ipv4 Header长度
#define PKT_HDR_IPV4_SIZE   (20)
// UDP头部长度
#define PKT_HDR_UDP_SIZE    (8)

// IPV4类型
#define IPV4    (0x0800)
// ARP类型
#define ARP     (0x0806)
// UDP类型
#define UDP     (0x11)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// l2结构，暂不考虑vlan
typedef struct{
    uint8_t mac_da[MAC_ADDR_SIZE];
    uint8_t mac_sa[MAC_ADDR_SIZE];
    uint16_t ether_type;
}attr_packed pkthdr_l2_t;
_Static_assert(sizeof(pkthdr_l2_t) == PKT_HDR_L2_SIZE, "sizeof pkthdr_l2_t must be 14");

// ipv4头部
typedef struct{
    uint8_t v_ihl;              // 4bit version + 4bit ihl(uint 4B)
    uint8_t tos;
    uint16_t tot_len;           // 总长度 头部+负载
    uint16_t id;
    uint16_t flag_frag;         // [15:13] flag, [12:0] frag offset
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;          // 包含ipv4 header
    uint32_t src_addr;
    uint32_t dst_addr;
}attr_packed pkthdr_ipv4_t;
_Static_assert(sizeof(pkthdr_ipv4_t) == PKT_HDR_IPV4_SIZE, "sizeof pkthdr_ipv4_t must be 20");

// UDP头部
typedef struct{
    uint16_t sport;             // 源端口
    uint16_t dport;             // 目的端口
    uint16_t len;               // UDP头部+负载长度
    uint16_t checksum;          // 校验和
}attr_packed pkthdr_udp_t;
_Static_assert(sizeof(pkthdr_udp_t) == PKT_HDR_UDP_SIZE, "sizeof pkthdr_udp_t must be 8");

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Static Variable                                */
/* ========================================================================== */

// ipv4 id全局计数器
static ATOMIC_UINT16_T g_ipv4_id = 0;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

/**
 * @brief       get ipv4 id
 * 
 * @note        内部自增
 */
static attr_force_inline uint16_t ipv4_id_get()
{
    return ATOM_FETCH_ADD(&g_ipv4_id, 1, MORDER_RELAXED);
}

/**
 * @brief       calculate 16bit checksum
 * 
 * @param[in]   buf     - 计算起始
 * @param[in]   len     - 长度
 * 
 * @retval      checksum, net order
 * 
 * @note        适用于ipv4 icmp
 */
static uint16_t calc_checksum(const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t sum = 0;

    // 按 16 位字累加（处理对齐问题，避免未对齐访问崩溃）
    while(len > 1)
    {
        sum += (uint16_t)((p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }

    // 处理奇数字节尾部：补零凑成 16 位
    if(len == 1)
        sum += (uint16_t)(p[0] << 8);

    // 将高 16 位进位折叠到低 16 位（最多两次即可收敛）
    sum = (sum & 0xFFFF) + (sum >> 16);
    sum = (sum & 0xFFFF) + (sum >> 16);

    // 取反码并转为网络字节序返回
    return htons((uint16_t)(~sum));
}

/**
 * @brief       check if CIDR format valid, parse it
 * 
 * @param[in]   cidr    - cidr string, such as 192.168.0.0/24
 * 
 * @param[out]  ip      - ip addr
 * @param[out]  prefix  - prefix
 * 
 * @retval      error code
 */
static error_code_e cidr_parse(const char *cidr, uint32_t *ip, uint8_t *prefix)
{
    if(!ip || !prefix)
        return ERR_BAD_PARAM;

    if(!cidr || *cidr == '\0')
        return ERR_NT_CIDR_INVALID;

    // 分离ip和前缀长度
    const char *slash = strchr(cidr, '/');  // 获取`/`指针
    if(!slash)
        return ERR_NT_CIDR_INVALID;
    if(NULL != strchr(slash+1, '/'))        // 检查'/'是否唯一
        return ERR_NT_CIDR_INVALID;
    size_t ip_len = slash - cidr;
    if(ip_len == 0 || ip_len >= INET_ADDRSTRLEN)
        return ERR_NT_CIDR_INVALID;
    char ip_str[INET_ADDRSTRLEN];           // ip字符串
    memcpy(ip_str, cidr, ip_len);
    ip_str[ip_len] = '\0';

    // 通过inet_pton校验语义
    struct in_addr addr;
    // inet_pton 会严格拒绝 "192.168.01.1"(前导零)、"192.168.0."(尾点) 等畸形格式
    if(inet_pton(AF_INET, ip_str, &addr) != 1)
        return ERR_NT_CIDR_INVALID;
    *ip = addr.s_addr;

    // 检查前缀长度
    const char *prefix_str = slash + 1;
    if(*prefix_str == '\0')
        return ERR_NT_CIDR_INVALID;

    if(prefix_str[0] == '0' && prefix_str[1] != '\0')   // 检查前导0
        return ERR_NT_CIDR_INVALID;

    if(prefix_str[0] < '1' || prefix_str[0] > '9')
        return ERR_NT_CIDR_INVALID;
    if(prefix_str[1] != '\0' && (prefix_str[1] < '0' || prefix_str[1] > '9'))
        return ERR_NT_CIDR_INVALID;
    if(prefix_str[1] != '0' && prefix_str[2] != '\0')
        return ERR_NT_CIDR_INVALID;

    int prefix_len = atoi(prefix_str);
    if(prefix_len < 0 || prefix_len > 32)
        return ERR_NT_CIDR_INVALID;
    *prefix = prefix_len;

    return ERR_NO_ERROR;
}

#endif
/* __NET_H__ end */