/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    mp_nonfixed.h
 * @brief   非固定大小内存池头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-09-01
 * @version 1.0
 *
 * @note    重构mp.c原来关于nonfixed_mp的实现
 *
 * @history
 *   1.0 | 2026-09-01 | cai | Initial creation.
 */

#ifndef __MP_NONFIXED_H__
#define __MP_NONFIXED_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdint.h>

#include "plat/compiler.h"
#include "type/type_hash.h"
#include "type/type_atom_queue.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 调试开关
#define MP_NONFIXED_DBG     (1)

/**
 * @brief       内部使用，声明非固定大小内存类型
 */
#define _declare_mem_type_attr_nonfixed(_name, _size)   \
mem_type_attr_nonfixed_t _mem_type_ ## _name = {    \
    .name = #_name, \
    .size = _size,  \
};  \
/* _declare_mem_type_attr_nonfixed end */

/**
 * @brief       外部使用，声明非固定大小内存池
 */
#define declare_mp_nonfixed(_name, _size)   \
    _declare_mem_type_attr_nonfixed(_name, aligned_8(_size))    \
/* declare_mp_nonfixed end */

/**
 * @brief       外部使用，注册非固定大小内存池，模块初始化调用
 */
#define mp_nonfixed_register(_name) \
    _mp_nonfixed_register(&_mem_type_ ## _name);    \
/* mp_nonfixed_register end */

/**
 * @brief       外部使用，extern非固定大小内存池
 */
#define declare_mp_nonfixed_extern(_name)   \
    extern mem_type_attr_nonfixed_t _mem_type_ ## _name;    \
/* declare_mp_nonfixed_extern */

/**
 * @brief       外部使用，申请内存
 */
#define mp_nonfixed_node_get(_name, _size)  \
    _mp_nonfixed_node_get(&_mem_type_ ## _name, aligned_8(_size));   \
/* mp_nonfixed_node_get end */

/**
 * @brief       外部使用，释放内存
 */
#define mp_nonfixed_node_put(ptr)   \
    _mp_nonfixed_node_put(ptr); \
/* mp_nonfixed_node_put end */

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 预定义hash，用于存储nonfixed mem attr
pre_declare_hash(mem_type_attr_nonfixed)

// 非固定大小内存类型
typedef struct{
    const char *name;           // name，全局唯一标识
    uint32_t size;              // 申请内存大小
    mem_type_attr_nonfixed_hash_item_t item;    // hash item

#if MP_NONFIXED_DBG
    ATOMIC_UINT32_T piece_cnt;          // 节点数量
    ATOMIC_UINT32_T free_size;          // 可用空闲内存
    ATOMIC_UINT32_T get_cnt;            // 申请次数
    ATOMIC_UINT32_T put_cnt;            // 释放次数
#endif
}mem_type_attr_nonfixed_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       init mp nonfixed attr
 */
extern void _mp_nonfixed_register(mem_type_attr_nonfixed_t *attr);

/**
 * @brief       init mp nonfixed module, top half
 */
extern void mp_nonfixed_module_init_th();

/**
 * @brief       init mp nonfixed module, bottom half
 */
extern void mp_nonfixed_module_init_bh();

/**
 * @brief       mp nonfixed module exit top half
 */
extern void mp_nonfixed_module_exit_th();

/**
 * @brief       mp nonfixed module exit bottom half
 */
extern void mp_nonfixed_module_exit_bh();

/**
 * @brief       get nonfixed node
 * 
 * @param[in]   attr
 * @param[in]   size
 * 
 * @retval      mem
 * 
 * @note        返回的可用内存大小按8对齐
 */
extern void* _mp_nonfixed_node_get(mem_type_attr_nonfixed_t *attr, uint32_t size);

/**
 * @brief       put nonfixed node
 */
extern void _mp_nonfixed_node_put(void *ptr);

/**
 * @brief       cli hook for: debug show mp nonfixed
 */
extern void mp_nonfixed_debug_cli_hook(unsigned char argc, char *argv[]);

#endif
/* __MP_NONFIXED_H__ end */