/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    compiler.h
 * @brief   编译属性头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-04-28
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-04-28 | cai | Initial creation.
 */

#ifndef __COMPILER_H__
#define __COMPILER_H__

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define attr_unused         __attribute__((unused))                         // 即使不使用，也不会报错
#define attr_pure           __attribute__((pure))                           // 纯函数：不会修改全局变量、传入指针的内存、无IO操作，返回值仅依赖于函数参数和某些全局/静态变量
#define attr_const          __attribute__((const))                          // 在Pure的基础上，不会读取任何全局状态或指针内存，优化会更加激进
#define attr_force_inline   inline __attribute__((unused, always_inline))
#define attr_pure_inline    inline __attribute__((unused, always_inline, pure))
#define attr_const_inline   inline __attribute__((unused, always_inline, const))
// ctor/dtor 避免vscode intellisense检查报错，实际gcc编译支持参数
#ifdef __INTELLISENSE__
#define attr_ctor(x)        
#else
#define attr_ctor(x)        __attribute__((constructor(x)))                 // 指定构造函数，在main函数执行前会自动运行
#endif
#ifdef __INTELLISENSE__
#define attr_dtor(x)        
#else
#define attr_dtor(x)        __attribute__((destructor(x)))
#endif

#define attr_aligned(n)     __attribute__((aligned(n)))                     // 指定起始地址必须是n的倍数，效果就是按n字节对齐
#define attr_cleanup(f)     __attribute__((unused, cleanup(f)))             // 指定cleanup函数，离开作用域后，自动调用f

// 构造、析构优先级，数值越小，优先级越高
#define CTOR_PRIO_LOW      (103)
#define CTOR_PRIO_MID      (102)
#define CTOR_PRIO_HIGH     (101)
#define DTOR_PRIO_LOW      (103)
#define DTOR_PRIO_MID      (102)
#define DTOR_PRIO_HIGH     (101)

#define thread_local        _Thread_local       // 线程本地属性

// 获取type类型member的偏移
#ifndef offsetof
#define offsetof(type, member) ((size_t) &((type *)0)->member)
#endif

// 根据指向member成员的ptr指针，反推指向type的指针
#ifndef container_of
#define container_of(ptr, type, member) \
    ({ \
        const typeof(((type *)0)->member) *__mptr = (ptr); \
        (type *)((char *)__mptr - offsetof(type, member)); \
    })
#endif

// 计算数组长度
#ifndef array_size
#define array_size(x)   (sizeof((x))/sizeof((x)[0]))
#endif

#endif