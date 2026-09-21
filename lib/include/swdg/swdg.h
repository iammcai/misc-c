/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    swdg.h
 * @brief   软件看门狗头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-08-27
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-08-27 | cai | Initial creation.
 */

#ifndef __SWDG_H__
#define __SWDG_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdint.h>

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/**
 * @brief       外部使用，注册自身到看门狗
 */
#define swdg_register(_name, threshold)     \
    _swdg_register(_name, threshold);   \
/* swdg_register end */

/**
 * @brief       外部使用，喂狗
 */
#define swdg_feed() \
    _swdg_feed();
/* swdg_feed end */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       register thread to swdg
 * 
 * @param[in]   name    - thd name
 * @param[in]   threshold   - timeout threshold
 * 
 * @note        提供业务线程注册看门狗
 */
extern void _swdg_register(const char *name, uint64_t threshold);

/**
 * @brief       feed swdg
 */
extern void _swdg_feed();

/**
 * @brief       cli cb for: show swdg
 */
extern void swdg_show_swdg_cli_cb(unsigned char argc, char *argv[]);

/**
 * @brief       init swdg module top half
 * 
 * @note        软件看门狗模块初始化
 */
extern void swdg_module_init_th();

/**
 * @brief       init swdg module bottom half
 * 
 * @note        软件看门狗模块初始化下半部，启动事件线程
 */
extern void swdg_module_init_bh();

#endif
/* __SWDG_H__ end */