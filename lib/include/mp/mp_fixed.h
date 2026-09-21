/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    mp_fixed.h
 * @brief   固定大小内存池头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-08-26
 * @version 1.0
 *
 * @note    之前mp.c实现的有隐蔽bug，这里重构一下，规范命名等
 *
 * @history
 *   1.0 | 2026-08-26 | cai | Initial creation.
 */

#ifndef __MP_FIXED_H__
#define __MP_FIXED_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdint.h>

#include "mp/mp.h"
#include "plat/compiler.h"
#include "type/type_hash.h"
#include "type/type_atom_queue.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// fixed mp调试开关
#define MP_FIXED_DBG        (1)

/**
 * @brief       内部使用，定义全局变量表示fixed mem type
 */
#define _declare_mem_type_fixed(_name, _node_cnt, _node_size, _flag)    \
mem_type_attr_fixed_t _mem_type_ ## _name = {   \
    .name = #_name, \
    .node_cnt = _node_cnt,  \
    .node_size = _node_size,    \
    .flag = _flag,  \
};  \
/* declare_mem_type_fixed end */

/**
 * @brief       外部使用，定义静态大小内存池静态变量
 */
#define declare_mp_fixed(_name, _node_cnt, _node_size)  \
    _declare_mem_type_fixed(_name, _node_cnt, _node_size, 0);   \
/* declare_mp_fixed end */

/**
 * @brief       外部使用，extern静态大小内存池静态变量，跨文件使用避免重复定义
 */
#define declare_mp_fixed_extern(_name)  \
    extern mem_type_attr_fixed_t _mem_type_ ## _name;
/* declare_mp_fixed end */

/**
 * @brief       外部使用
 */

/**
 * @brief       外部使用，注册静态大小内存类型。业务初始化需要调用
 */
#define mem_type_attr_fixed_register(_name)    \
    _mem_type_attr_fixed_register(&_mem_type_ ## _name);    \
/* mem_type_attr_fixed_register end */

/**
 * @brief       外部使用，申请内存
 */
#define mp_fixed_node_get(_name)    \
    _mp_fixed_node_get(&_mem_type_ ## _name);   \
/* mp_fixed_node_get end */

/**
 * @brief       外部使用，释放内存
 */
#define mp_fixed_node_put(data) \
    _mp_fixed_node_put(data);
/* mp_fixed_node_put end */

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

typedef struct mem_type_attr_fixed_s mem_type_attr_fixed_t;

// 预定义hash用于存储所有fixed mem type attr变量
pre_declare_hash(mem_type_attr_fixed)

// 固定大小内存类型定义
struct mem_type_attr_fixed_s{
    const char *name;       // name，全局标识
    uint32_t node_cnt;      // 内存节点数量
    uint16_t node_size;     // 内存节点个数
    uint32_t flag;          // 标志
#if MP_FIXED_DBG
    ATOMIC_UINT32_T used;   // 使用中的个数，包括用户占用和aq中的
    ATOMIC_UINT32_T free;   // freelist中的个数，可直接分配
#endif
    mem_type_attr_fixed_hash_item_t item;       // hash item
};

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       register mem type attr fixed
 * 
 * @param[in]   attr
 * 
 * @note        将mem type attr注册到全局hash
 */
extern void _mem_type_attr_fixed_register(mem_type_attr_fixed_t *attr);

/**
 * @brief       alloc node from fixed mp
 * 
 * @param[in]   attr    - mem type attr
 * 
 * @retval      mem
 */
extern void* _mp_fixed_node_get(mem_type_attr_fixed_t *attr);

/**
 * @brief       free data into fixed mp
 * 
 * @param[in]   data
 */
extern void _mp_fixed_node_put(void *data);

/**
 * @brief       init mp fixed module top half
 */
extern void mp_fixed_module_init_th();

/**
 * @brief       init mp fixed module bottom half
 */
extern void mp_fixed_module_init_bh();

/**
 * @brief       cli cb for: debug show mp fixed
 */
extern void mp_fixed_debug_show(unsigned char argc, char *argv[]);

#endif