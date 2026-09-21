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
#include "cli/cli.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

extern cli_param_t arp_probe_param[];

extern cli_param_t arp_scan_param[];

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       arp module init
 */
extern void arp_module_init(void);

/**
 * @brief       arp module deinit
 */
extern void arp_module_deinit();

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

/**
 * @brief       cli hook for arp probe
 * 
 * @param[in]   target_ip
 */
extern void arp_probe_cli_hook(unsigned char argc, char *argv[]);

/**
 * @brief       cli hook for arp scan
 */
extern void arp_scan_cli_hook(unsigned char argc, char *argv[]);

/**
 * @brief       cli hook for show arp cache
 * 
 * @note        打印arp cache
 */
extern void arp_show_cache_cli_hook(unsigned char argc, char *argv[]);

#endif
/* #ifndef __ARP_H_ end */