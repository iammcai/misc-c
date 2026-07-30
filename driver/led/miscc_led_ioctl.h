/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    miscc_led_ioctl.h
 * @brief   led约定ioctl命令的头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-07-30
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-07-30 | cai | Initial creation.
 */

#ifndef __MISCC_LED_IOCTL_H__
#define __MISCC_LED_IOCTL_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <linux/ioctl.h>

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// IOCTL的命令号，led的ASCII
#define LED_IOCTL_MAGIC         (0x6C6564)
// 定义用户态往内核态写入的命令，_IOW宏用来生成Write方向命令
#define LED_IOCTL_SET_STATE     _IOW(LED_IOCTL_MAGIC, 0, int)
// 定义用户态从内核态读出的命令，_IOR宏用来生成Read方向命令
#define LED_IOCTL_GET_STATE     _IOR(LED_IOCTL_MAGIC, 1, int)

#endif