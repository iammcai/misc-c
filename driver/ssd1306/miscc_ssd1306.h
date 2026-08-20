/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    miscc_ssd1306.h
 * @brief   ssd1306头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-08-20
 * @version 1.0
 *
 * @note    约定屏幕使用方式：
 *          - 使用8*16字体，对于0.96寸只有4行*16个字符
 *          - 初始化后屏幕有初始信息，用户态首次写入需要清除屏幕
 *          - 用户态写入直接write传入需要的字符串
 *          - 每次写入位置是一行的左侧起始，0，1，2，3依次行递增，写满需要清除
 *          - 清除屏幕：写入SSD1306_CLEAR
 *
 * @history
 *   1.0 | 2026-08-20 | cai | Initial creation.
 */

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 清除SSD1306屏幕命令
#define SSD1306_CLEAR   "?clear"

// SSD写入长度，一行最多16个字符
#define SSD1306_BUF_SIZE            17