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
#include <arpa/inet.h>
#include "plat/compiler.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// MAC地址长度
#define MAC_ADDR_SIZE   (6)
// IPV4地址长度
#define IPV4_ADDR_SIZE  (4)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// l2结构，暂不考虑vlan
typedef struct{
    uint8_t mac_da[MAC_ADDR_SIZE];
    uint8_t mac_sa[MAC_ADDR_SIZE];
    uint16_t ether_type;
}attr_packed pkthdr_l2_t;
_Static_assert(sizeof(pkthdr_l2_t) == 14, "sizeof pkthdr_l2_t must be 14");

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

#endif
/* __NET_H__ end */