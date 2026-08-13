/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    web.h
 * @brief   web管理头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-29
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-29 | cai | Initial creation.
 */

#ifndef __WEB_H__
#define __WEB_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "web/http.h"
#include "plat/compiler.h"
#include "plat/debug.h"

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       int web module
 */
extern void web_module_init();

/**
 * @brief       de init web module
 */
extern void web_module_deinit();

#endif