/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    arp.h
 * @brief   arp相关功能头文件
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

#ifndef __ARP_H__
#define __ARP_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "net_comm.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

/**
 * @brief       arp module init
 */
extern void arp_module_init(void);

/**
 * @brief       send one arp request
 * 
 * @param[in]   if_name - if name
 * @param[in]   dst_ip  - dip, host order
 * 
 * @retval      error code
 * 
 * @note        发送一个arp请求
 */
extern error_code_e arp_request_send(const char *if_name, uint32_t dst_ip);

/**
 * @brief       query arp cache
 * 
 * @param[in]   ip  - ip addr, host order
 * 
 * @param[out]  mac - mac addr
 * 
 * @retval      error code
 */
extern error_code_e arp_cache_query(uint32_t ip, uint8_t *mac);

#endif
/* #ifndef __ARP_H_ end */