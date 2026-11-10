/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    bh1750.h
 * @brief   bh1750用户态头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-11-10
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-11-10 | cai | Initial creation.
 */

#ifndef __BH1750_H__
#define __BH1750_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "plat/compiler.h"
#include "plat/debug.h"
#include "cli/cli.h"

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       init bh1750 module
 * 
 * @note        加载驱动
 */
extern void bh1750_module_init();

/**
 * @brief       exit bh1750 module
 * 
 * @note        卸载驱动
 */
extern void bh1750_module_exit();

/**
 * @brief       read lux
 * 
 * @param[out]  lux
 * 
 * @retval      error code
 */
extern error_code_e bh1750_lux_read(double *lux);

/**
 * @brief       cli cb for: show light
 */
extern void bh1750_light_dump_cli_cb(cli_ctx_t *ctx, unsigned char argc, char *argv[]);

#endif