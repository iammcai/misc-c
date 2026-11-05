/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    zcap.h
 * @brief   抓包头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-25
 * @version 1.1
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-11 | cai | Initial creation.
 *   1.1 | 2026-06-25 | cai | Use event_loop and hash handle for analyzing pkt.
 */

#ifndef __ZCAP_H__
#define __ZCAP_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <pthread.h>

#include "plat/compiler.h"
#include "plat/atom.h"
#include "type/type_atom_queue.h"
#include "msg_q/msg_q.h"
#include "type/type_hash.h"
#include "event/ev_lock.h"
#include "net_comm.h"
#include "cli/cli.h"
#include "type/type_heap.h"
#include "json_util/json_util.h"
#include "dissect/tcp.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 过滤字段长度
#define FIELD_DATA_SIZE     (6)
// 过滤掩码长度
#define FIELD_MASK_SIZE     (6)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 匹配filter执行的回调
typedef void (*zcap_pkt_filter_match_cb)(char *packet, int len, void *args);

// 报文条件注册字段枚举
typedef enum{
    MAC_DA,     // 目的mac
    MAC_SA,     // 源mac
    LEN_TYPE,   // ether type
    IP_DA,      // 目的ip
    IP_SA,      // 源ip
    PRO_TYPE,   // ip protocol
    DPORT,      // 目的端口号
    SPORT,      // 源端口号
}zcap_pkt_field_e;

// 抓包结构声明
typedef struct zcap_s zcap_t;

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/**
 *  外部使用，声明抓包类型变量
 */
#define declare_zcap(_ifname)   \
    _zcap_declare(#_ifname);    \
/* declare_zcap end */

/**
 * 外部使用，启动抓包
 */
#define zcap_start(_ifname)     \
    _zcap_start(#_ifname);      \
/* zcap_start end */

/**
 * 外部使用，结束抓包
 */
#define zcap_cancel(_ifname)    \
    _zcap_cancel(#_ifname);     \
/* zcap_cancel end */

/**
 * 外部使用，注册过滤项
 */
#define zcap_register_pkt_filter(_ifname, _filter_name, _cb, _args) \
    _zcap_register_pkt_filter(_ifname, _filter_name, _cb, _args);   \
/* zcap_register_pkt_filter end */

/**
 * 内部使用，注册过滤字段
 */
#define zcap_register_field(_ifname, _filter_name, field_type, data, mask)  \
    _zcap_register_field(_ifname, _filter_name, field_type, data, mask);    
/* zcap_register_field end */

/**
 * 外部使用，注册len type字段
 */
#define zcap_register_field_len_type(_ifname, _filter_name, _data, _mask) do  { \
    uint8_t data[FIELD_DATA_SIZE];  \
    uint8_t mask[FIELD_MASK_SIZE];  \
    data[0] = (_data >> 8) & 0xff;  \
    data[1] = (_data >> 0) & 0xff;  \
    mask[0] = (_mask >> 8) & 0xff;  \
    mask[1] = (_mask >> 0) & 0xff;  \
    zcap_register_field(_ifname, _filter_name, LEN_TYPE, data, mask);   \
}while(0);  \

/**
 * 外部使用，注册ip protocol字段
 */
#define zcap_register_field_pro_type(_ifname, _filter_name, _data, _mask) do  { \
    uint8_t data[FIELD_DATA_SIZE];  \
    uint8_t mask[FIELD_MASK_SIZE];  \
    data[0] = (_data) & 0xff;   \
    mask[0] = (_mask) & 0xff;   \
    zcap_register_field(_ifname, _filter_name, PRO_TYPE, data, mask);   \
}while(0);  \

/**
 * 外部使用，注册dst port字段
 */
#define zcap_register_field_l4_dport(_ifname, _filter_name, _data, _mask) do  { \
    uint8_t data[FIELD_DATA_SIZE];  \
    uint8_t mask[FIELD_MASK_SIZE];  \
    data[0] = (_data >> 8) & 0xff;  \
    mask[0] = (_mask >> 8) & 0xff;  \
    data[1] = (_data) & 0xff;       \
    mask[1] = (_mask) & 0xff;       \
    zcap_register_field(_ifname, _filter_name, DPORT, data, mask);  \
}while(0);  \
/* zcap_register_field_l4_dport end */

/**
 * 外部使用，注册src port字段
 */
#define zcap_register_field_l4_sport(_ifname, _filter_name, _data, _mask) do  { \
    uint8_t data[FIELD_DATA_SIZE];  \
    uint8_t mask[FIELD_MASK_SIZE];  \
    data[0] = (_data >> 8) & 0xff;  \
    mask[0] = (_mask >> 8) & 0xff;  \
    data[1] = (_data) & 0xff;       \
    mask[1] = (_mask) & 0xff;       \
    zcap_register_field(_ifname, _filter_name, SPORT, data, mask);  \
}while(0);  \
/* zcap_register_field_l4_dport end */

/* ========================================================================== */
/*                              Extern Symbols                                */
/* ========================================================================== */

extern cli_param_t zcap_dump_pkt_param[];

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       zcap module init
 */
extern void zcap_module_init();

/**
 * @brief       zcap module exit top half
 * 
 * @note        系统退出时，zcap需要立即停止抓包，避免产生消息到模块
 */
extern void zcap_module_exit_th();

/**
 * @brief       zcap module exit bottom half
 */
extern void zcap_module_exit_bh();

/**
 * @brief       内部使用，创建抓包管理变量
 * 
 * @param[in]   name    - if name
 */
extern void _zcap_declare(const char *name);

/**
 * @brief       内部使用，初始化抓包组件，未启动
 * 
 * @param[in]   captor  - captor
 */
extern void _zcap_init(zcap_t *captor);

/**
 * @brief       启动抓包线程
 * 
 * @param[in]   name
 */
extern void _zcap_start(const char *name);

/**
 * @brief       结束抓包线程
 * 
 * @param[in]   name
 */
extern void _zcap_cancel(const char *name);

/**
 * @brief       get zcaptor status
 * 
 * @param[in]   if_name     - interface name
 * 
 * @retval      0 - shutdown, 1 - running
 */
extern int zcap_status_get(const char *if_name);

/**
 * @brief       cb for http cgi: /api/network/status/rx
 */
extern error_code_e zcap_rx_http_cgi_cb(json_builder_t *jb);

/**
 * @brief       cb for http cgi: /api/sync/ zcap rx module
 * 
 * @note        不含{}版本
 */
extern error_code_e zcap_sync_network_http_cgi_cb(json_builder_t *jb);

/**
 * @brief       cb for http cgi: /api/network/stream/pie
 */
extern error_code_e zcap_stream_pie_cgi_cb(json_builder_t *jb);

/**
 * @brief       cb for http cgi: /api/sync/ zcap pie module
 * 
 * @note        不含{}版本
 */
extern error_code_e zcap_sync_stream_pie_cgi_cb(json_builder_t *jb);

/**
 * @brief       cb for http cgi: /api/network/protocol/bar
 */
extern error_code_e zcap_protocol_bar_cgi_cb(json_builder_t *jb);

/**
 * @brief       cb for http cgi: /api/sync/ zcap protocol bar module
 * 
 * @note        不含{}版本
 */
extern error_code_e zcap_sync_protocol_bar_cgi_cb(json_builder_t *jb);

/**
 * @brief       register filter into zcaptor
 * 
 * @param[in]   if_name         - if name
 * @param[in]   filter_name     - filter name
 * @param[in]   cb              - match cb
 * @param[in]   args            - cb args
 */
extern void _zcap_register_pkt_filter(const char *if_name, const char *filter_name, zcap_pkt_filter_match_cb cb, void *args);

/**
 * @brief       register field into filter
 * 
 * @param[in]   if_name     - if name
 * @param[in]   filter_name - filter name
 * @param[in]   field       - field of pkt
 * @param[in]   data        - field data
 * @param[in]   mask        - field mask
 */
extern void _zcap_register_field(const char *if_name, const char *filter_name, zcap_pkt_field_e field, uint8_t *data, uint8_t *mask);

/**
 * @brief       cli hook for: show zcaptor rx
 * 
 * @note        打印zcaptor rx信息
 */
extern void zcap_dump_captor_rx_cli_hook(cli_ctx_t *ctx, unsigned char argc, char *argv[]);

/**
 * @brief       cli hook for: show zcaptor tx
 * 
 * @note        打印zcaptor tx信息
 */
extern void zcap_dump_captor_tx_cli_hook(cli_ctx_t *ctx, unsigned char argc, char *argv[]);

/**
 * @brief       cli hook for: zcap dump
 * 
 * @note        开启逐包报文打印一段时间
 */
extern void zcap_dump_pkt_cli_hook(cli_ctx_t *ctx, unsigned char argc, char *argv[]);

/**
 * @brief       cli hook for: show zcaptor src top
 * 
 * @note        打印流量来源TOP3
 */
extern void zcap_dump_src_top_cli_hook(cli_ctx_t *ctx, unsigned char argc, char *argv[]);

#endif
/* __ZCAP_H__ end */