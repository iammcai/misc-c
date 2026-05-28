/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    mp.h
 * @brief   内存池头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-05-27
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-05-27 | cai | Initial creation.
 */

#ifndef __MP_H__
#define __MP_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <assert.h>
#include <string.h>
#include "type/type_list.h"
#include "type/type_hash.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 预定义哈希表存储内存类型属性
pre_declare_hash(mem_type_attr)

// 内存属性枚举
typedef enum{
    MEM_TYPE_ATTR_FIXED_SIZE = 1 << 0,      // 节点大小固定
}mem_type_attr_flag_e;

// 内存类型属性
typedef struct{
    const char *name;                   // 类型名
    unsigned int flag;                  // 标志位
    unsigned int node_size;             // 该类型的内存节点大小
    unsigned int node_max_num;          // 该类型的最大节点数量
    mem_type_attr_hash_item_t item;     // hash item
}mem_type_attr_t;

// 预定义固定大小内存空闲链表
pre_declare_list(fixed_free)

// 固定大小内存节点定义
typedef struct{
    mem_type_attr_t *attr;                  // 所属内存类型
    fixed_free_list_item_t item;   // free list item
    unsigned int size;                      // 用户内存大小
    char attr_aligned(8) data[];            // 柔性数组，真正给用户使用的内存区
} attr_aligned(8) fixed_mem_node_t;

// 预定义哈希表，用于存储所有固定大小空闲链表头
pre_declare_hash(fixed_free_list_head)

// 固定大小空闲内存链表头定义，哈希表存储起来
typedef struct{
    fixed_free_list_head_t  head;   // 空闲链表头
    mem_type_attr_t *attr;          // 所属内存类型
    fixed_free_list_head_hash_item_t item;  // item
}fixed_free_list_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

// 定义固定大小内存空闲链表操作
declare_list(fixed_free, fixed_free, fixed_mem_node_t, item)

/**
 * @brief       封装hash add，加到全局哈希表中
 * 
 * @param[in]   attr    - mem type attr
 */
extern void mem_type_attr_init(mem_type_attr_t *attr);

/**
 * @brief       初始化fixed node mem pool
 * 
 * @param[in]   attr    - mem type attr
 * 
 * @note        查找空闲链表，（创建并）补充节点数量
 */
extern void _mp_fixed_init(mem_type_attr_t *attr);

/**
 * @brief       supply mem node from system to free list
 * 
 * @param[in]   head    - free list head
 * @param[in]   attr    - mem type attr
 */
extern void fixed_free_list_supply(fixed_free_list_head_t *head, mem_type_attr_t *attr);

/**
 * @brief       get node from fixed mem pool
 * 
 * @param[in]   attr    - mem type
 * 
 * @retval      ptr to buffer
 */
extern void* _mp_fixed_node_get(mem_type_attr_t *attr);

/**
 * @brief       put node to fixed mem pool
 * 
 * @param[in]   ptr     - ptr to buffer
 */
extern void _mp_fixed_node_put(void *ptr);

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 内部使用，定义内存类型，定义变量并构造加入hash
#define _declare_mem_type_attr(_name, _flag, _node_size, _node_max_num)  \
static mem_type_attr_t _mem_type_attr_ ## _name = {  \
    .name = #_name, \
    .flag = _flag,  \
    .node_size = _node_size,    \
    .node_max_num = _node_max_num   \
};  \
static attr_force_inline void _mem_type_attr_ ## name ## _init() attr_ctor(CTOR_PRIO_LOW);  \
static inline void _mem_type_attr_ ## name ## _init()   \
{   \
    mem_type_attr_init(&_mem_type_attr_ ## _name);    \
}   \
/* _declare_mem_type_attr end */

/**
 * 外部使用，声明一类固定大小的内存类型
 */
#define declare_mem_type_fixed(name, node_size, node_max_num)   \
    _declare_mem_type_attr(name, MEM_TYPE_ATTR_FIXED_SIZE, node_size, node_max_num) \
/* declare_mem_type_fixed end */

/**
 * 外部使用，初始化一个固定节点大小内存池
 * 0. 初始化全局空闲链表
 * 1. 在【线程私有】的全局空闲链表头哈希表中查找有无空闲链表
 *  1.1 无的话，创建并加入哈希表
 * 2. 根据指定节点数量和大小，从系统分配内存
*/
#define mp_fixed_init(name) \
    _mp_fixed_init(&_mem_type_attr_ ## name);   \
/* declare_mp_fixed */

/**
 * 外部使用，从内存池获取一个固定大小的内存结点
 */
#define mp_fixed_node_get(name) \
    _mp_fixed_node_get(&_mem_type_attr_ ## name);   \
/* mp_fixed_node_get end */

/**
 * 外部使用，归还一个固定大小内存节点给内存池
 */
#define mp_fixed_node_put(ptr)  \
    _mp_fixed_node_put(ptr);    \

/* ========================================================================== */
/*                              Debug Function                                */
/* ========================================================================== */

#define MP_TRACE    (1)

#if MP_TRACE

#include <stdio.h>

/**
 * @brief       dump all fixed free list info
 */
extern void mp_dump_fixed_free_list();

#else

#define mp_dump_fixed_free_list()   

#endif

#endif
/* __MP_H__ end*/