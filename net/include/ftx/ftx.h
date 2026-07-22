/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    ftx.h
 * @brief   发包头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-07-08
 * @version 1.0
 *
 * @note    flexible tx
 *
 * @history
 *   1.0 | 2026-07-08 | cai | Initial creation.
 */

#ifndef __FTX_H__
#define __FTX_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "net.h"
#include "plat/compiler.h"
#include "type/type_hash.h"
#include "plat/debug.h"
#include "mp/mp.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 预定义hash，管理所有ftx_t变量
pre_declare_hash(ftx)

// ftx别名
typedef struct ftx_s ftx_t;

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/**
 * 外部使用，声明初始化一个发包接口
 */
#define declare_ftx(_if_name)   \
    _ftx_init(#_if_name);   \
/* declare_ftx end */

/**
 * 外部使用，发送自定义报文
 */
#define ftx_send(_if_name, ctx, len)    \
    _ftx_send(_if_name, ctx, len);  \
/* ftx_send end */

/**
 * 外部使用，发送TCP报文
 */

/**
 * 外部使用，发送UDP报文
 */
#define ftx_send_udp(_if_name, dip, dport, ctx, len)    \
    _ftx_send_udp(_if_name, dip, dport, ctx, len);  \

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       init ftx module
 */
extern void ftx_module_init(void);

/**
 * @brief       init a ftxor
 * 
 * @param[in]   if_name     - if name
 * 
 * @note        内部使用，初始化发包管理结构
 */
extern void _ftx_init(const char *if_name);

/**
 * @brief       send packet
 * 
 * @param[in]   if_name - interface name
 * @param[in]   ctx     - packet
 * @param[in]   len     - packet length
 */
extern void _ftx_send(const char *if_name, void *ctx, unsigned int len);

/**
 * @brief       send udp packet
 * 
 * @param[in]   if_name     - interface name
 * @param[in]   dip         - dest ip, host order
 * @param[in]   dport       - dest port, host order
 * @param[in]   ctx         - udp payload
 * @param[in]   len         - length of ctx
 * 
 * @note        使用sendto发送，暂不使用sendmsg
 */
extern void _ftx_send_udp(const char *if_name, uint32_t dip, uint16_t dport, void *ctx, unsigned int len);

/**
 * @brief       get mac addr by interface name
 * 
 * @param[in]   if_name     - if name
 * 
 * @param[out]  mac_addr    - u8 array, size is 6
 * 
 * @retval      error code
 */
extern error_code_e ftx_mac_get(const char *if_name, uint8_t *mac_addr);

/**
 * @brief       get ipv4 addr by interface name
 * 
 * @param[in]   if_name     - if name
 * 
 * @param[out]  ip_addr     - uint32, net order
 * 
 * @retval      error code
 */
extern error_code_e ftx_ip_get(const char *if_name, uint32_t *ip_addr);

#endif
/* __FTX_H__ end */