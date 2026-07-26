/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    tftp.h
 * @brief   tftp头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-07-27
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-07-27 | cai | Initial creation, support tftp client.
 */

#ifndef __TFTP_H__
#define __TFTP_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "ftx/ftx.h"
#include "zcap/zcap.h"
#include "net.h"
#include "cli/cli.h"

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       tftp module init
 */
extern void tftp_module_init();

#endif