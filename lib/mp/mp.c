/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    mp.c
 * @brief   内存池实现
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

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "mp/mp.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 获取node中的data给用户
#define mp_fixed_node_data(_node)   (_node)->data;
// 根据data获取node
#define mp_fixed_data_node(_data)   container_of(_data, fixed_mem_node_t, data)

/* ========================================================================== */
/*                           Static Function Prototypes                       */
/* ========================================================================== */

/**
 * @brief       mem type attr cmp func
 * 
 * @param[in]   a1  - attr 1
 * @param[in]   a2  - attr 2
 * 
 * @retval      cmp val
 * 
 * @note        cmp by name
 */
static attr_pure_inline int _mem_type_attr_cmp(mem_type_attr_t *a1, mem_type_attr_t *a2)
{
    assert(a1 && a2 && a1->name && a2->name);
    return strcmp(a1->name, a2->name);
}

/**
 * @brief       free list cmp func
 * 
 * @param[in]   l1  - list 1
 * @param[in]   l2  - list 2
 * 
 * @retval      cmp val
 * 
 * @note        cmp by attr name
 */
static attr_pure_inline int _fixed_free_list_cmp(fixed_free_list_t *l1, fixed_free_list_t *l2)
{
    assert(l1 && l1->attr && l1->attr->name && l2 && l2->attr && l2->attr->name);
    return strcmp(l1->attr->name, l2->attr->name);
}

/**
 * @brief       mem type attr hash func
 * 
 * @param[in]   a   - attr
 * 
 * @retval      hash val
 * 
 * @note        hash by name
 */
static attr_pure_inline unsigned int _mem_type_attr_hash(mem_type_attr_t *a)
{
    assert(a && a->name);
    return type_hash_jhash(a->name, strlen(a->name), 0);
}

/**
 * @brief       fixed free list hash func
 * 
 * @param[in]   l   - list
 * 
 * @retval      hash val
 * 
 * @note        hash by attr name
 */
static attr_pure_inline unsigned int _fixed_free_list_hash(fixed_free_list_t *l)
{
    assert(l && l->attr && l->attr->name);
    return type_hash_jhash(l->attr->name, strlen(l->attr->name), 0);
}

/**
 * @brief       check if attr fixed_size
 * 
 * @param[in]   attr    - mem type attr
 * 
 * @retval      0 - no, else - yes
 */
static attr_pure_inline unsigned int mem_type_attr_fixed_size(mem_type_attr_t *attr)
{
    return attr->flag & MEM_TYPE_ATTR_FIXED_SIZE;
}

/**
 * @brief       call system calloc
 * 
 * @param[in]   num     - element num
 * @param[in]   size    - elememt size
 * 
 * @retval      ptr to memory
 */
static attr_force_inline void* mp_calloc(size_t num, size_t size)
{
    return calloc(num, size);
}

/**
 * @brief       new fixed mem node from system
 * 
 * @param[in]   attr    - mem type attr
 * 
 * @retval      ptr to node
 */
static attr_force_inline fixed_mem_node_t* fixed_mem_node_new(mem_type_attr_t *attr)
{
    assert(mem_type_attr_fixed_size(attr));     // 检查是否固定大小

    fixed_mem_node_t* node = mp_calloc(1, sizeof(fixed_mem_node_t)+attr->node_size);    // 柔性数组分配需添加node_size
    assert(node);

    node->size = attr->node_size;
    node->attr = attr;

    return node;
}

/**
 * @brief       init gloabl variable: g_mem_type_attr_hash_head
 * 
 * @note        CTOR
 */
static attr_force_inline void g_mem_type_attr_hash_init() attr_ctor(CTOR_PRIO_MID);

/**
 * @brief       init gloabl variable: g_fixed_free_list_hash_head
 * 
 * @note        CTOR
 */
static attr_force_inline void g_fixed_free_list_head_hash_init() attr_ctor(CTOR_PRIO_MID);

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

// 全局哈希表，存储所有内存类型属性
static mem_type_attr_hash_head_t g_mem_type_attr_hash_head = {};
// 全局哈希表，存储所有固定大小空闲链表头
static fixed_free_list_head_hash_head_t g_fixed_free_list_hash_head = {};

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

// 定义内存类型属性哈希表相关操作
declare_hash(mem_type_attr, mem_type_attr, mem_type_attr_t, item, 1, 31, _mem_type_attr_cmp, _mem_type_attr_hash)

// 定义空闲链表头链表相关操作
declare_hash(fixed_free_list_head, fixed_free_list_head, fixed_free_list_t, item, 1, 31, _fixed_free_list_cmp, _fixed_free_list_hash)

static inline void g_mem_type_attr_hash_init()
{
    mem_type_attr_hash_init(&g_mem_type_attr_hash_head);
}

static inline void g_fixed_free_list_head_hash_init()
{
    fixed_free_list_head_hash_init(&g_fixed_free_list_hash_head);
}

void mem_type_attr_init(mem_type_attr_t *attr)
{
    assert(attr && attr->name);
    mem_type_attr_hash_add(&g_mem_type_attr_hash_head, attr);
}

void fixed_free_list_head_init(fixed_free_list_t *ffl)
{
    assert(ffl);
    fixed_free_list_head_hash_add(&g_fixed_free_list_hash_head, ffl);
}

void fixed_free_list_supply(fixed_free_list_head_t *head, mem_type_attr_t *attr)
{
    int count = 0;
    fixed_mem_node_t *node = NULL;

    assert(head && attr);

    while(fixed_free_list_count(head) < attr->node_max_num)
    {
        node = fixed_mem_node_new(attr);
        fixed_free_list_add_head(head, node);
    }
}

void* _mp_fixed_node_get(mem_type_attr_t *attr)
{
    if(!mem_type_attr_fixed_size(attr))     // 检查是否固定大小
        return NULL;

    // 找到free list
    fixed_free_list_t key = {.attr = attr};
    fixed_free_list_t *free_list = fixed_free_list_head_hash_find(&g_fixed_free_list_hash_head, &key);
    assert(free_list);

    if(!fixed_free_list_count(free_list->head)) // 检查空闲节点个数
        return NULL;

    fixed_mem_node_t *node = fixed_free_list_pop(free_list->head);  // pop一个节点

    return mp_fixed_node_data(node);
}

void _mp_fixed_node_put(void *ptr)
{
    assert(ptr);
    fixed_mem_node_t *node = mp_fixed_data_node(ptr);
    assert(mem_type_attr_fixed_size(node->attr));       // 确认是否fixed

    fixed_free_list_t key = {.attr = node->attr};
    fixed_free_list_t *free_list = fixed_free_list_head_hash_find(&g_fixed_free_list_hash_head, &key);  // 找到freelist
    assert(free_list);

    fixed_free_list_add_head(free_list->head, node);    // 返回空闲链表
}

/* ========================================================================== */
/*                              Debug Function                                */
/* ========================================================================== */

#if MP_TRACE

void mp_dump_fixed_free_list()
{
    fixed_free_list_t *ffl = NULL;

    printf("dump fixed free list\n");

    ffl = fixed_free_list_head_hash_first(&g_fixed_free_list_hash_head);
    while(ffl)
    {
        fixed_free_list_head_t *head = ffl->head;

        fixed_mem_node_t *node = fixed_free_list_first(head);
        while(node)
        {
            printf("pos %p, attr->name %s, size %u, user pos %p\n", node, node->attr->name, node->size, node->data);
            node = fixed_free_list_next(node);
        }
        printf("========\n");

        ffl = fixed_free_list_head_hash_next(&g_fixed_free_list_hash_head, ffl);
    }
}

#endif