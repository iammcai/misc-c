/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    zcap.h
 * @brief   抓包头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-11
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-11 | cai | Initial creation.
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

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 帧大小，2KB，足够满足普通以太网报文
#define FRAME_SIZE      (2048)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 预定义hash，用于存zcap_t
pre_declare_hash(zcaptor)

// 报文统计结构
typedef struct{
    ev_mutex_t mtx;             // 互斥锁
    unsigned long count;        // 总数量
    unsigned long size;         // 总大小，单位Byte

    //以下用于速率统计
    unsigned long curr_count;   // 本次统计数量
    unsigned long curr_size;    // 本次统计大小
    unsigned long last_count;   // 上次统计数量
    unsigned long last_size;    // 上次统计大小
    double rate_pps;            // rx pps
    double rate_Bps;            // rx Bps
}zcap_stat_t;

// 预定义spsc_aq，用于存放alz_t和rx_t交互报文
pre_declare_spsc_atom_queue(zcap_packet)

// 报文定义
typedef struct{
    char packet[FRAME_SIZE];    // 报文内容
    unsigned int len;           // 报文长度
    zcap_packet_spsc_atom_queue_item_t item;    // aq item
}zcap_packet_t;

// 抓包结构定义
typedef struct{
    const char *if_name;    // 监听接口名
    int sock_fd;            // 监听所用socket fd
    int if_index;           // 接口对应内核index
    ATOMIC_UINT8_T ready;   // 是否初始化完毕
    void *ring_buffer;      // 映射
    unsigned long ring_len; // 映射大小

    pthread_t rx_t;         // 抓包线程
    ATOMIC_UINT8_T running; // 启动标志

    pthread_t alz_t;            // 解析线程
    ATOMIC_UINT8_T analyzing;   // 正在解析
    zcap_stat_t stat;           // 统计数据

    zcap_packet_spsc_atom_queue_head_t aq_head; // 用于rx_t传递报文给alz_t

    zcaptor_hash_item_t item;   // hash item
}zcap_t;

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/**
 *  外部使用，声明抓包，然后初始化
 * 1. 定义全局static zcap_t 类型变量
 * 2. 执行初始化函数
 */
#define declare_zcap(_ifname) \
static zcap_t _ifname ## _zcaptor = {   \
    .if_name = #_ifname,    \
    .sock_fd = -1,  \
    .if_index = -1, \
    .ring_buffer = NULL,    \
    .ring_len = 0,  \
    .ready = 0, \
    .rx_t = 0,  \
    .running = 0,   \
    .alz_t = 0, \
    .analyzing = 0, \
    .stat.mtx = EV_MUTEX_INITIALIZER,   \
};  \
_zcap_init(&_ifname ## _zcaptor);   \
/* _zcap_init end */

/**
 * 外部使用，启动抓包
 */
#define zcap_start(_ifname) \
    _zcap_start(&_ifname ## _zcaptor);  \
/* zcap_start end */

/**
 * 外部使用，结束抓包
 */
#define zcap_cancel(_ifname)    \
    _zcap_cancel(&_ifname ## _zcaptor);
/* zcap_cancel end */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       内部使用，初始化抓包组件，未启动
 * 
 * @param[in]   captor  - captor
 */
extern void _zcap_init(zcap_t *captor);

/**
 * @brief       启动抓包线程
 * 
 * @param[in]   captor  - captor
 */
extern void _zcap_start(zcap_t *captor);

/**
 * @brief       结束抓包线程
 * 
 * @param[in]   captor  - captor
 */
extern void _zcap_cancel(zcap_t *captor);

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

#endif
/* __ZCAP_H__ end */