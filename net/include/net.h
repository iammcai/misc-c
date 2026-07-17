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

// IPV4类型
#define IPV4    (0x0800)
// ARP类型
#define ARP     (0x0806)

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

#endif
/* __NET_H__ end */