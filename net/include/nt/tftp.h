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
#include "net_comm.h"
#include "cli/cli.h"

/* ========================================================================== */
/*                             Extern Symbols                                 */
/* ========================================================================== */

extern cli_param_t tftp_dl_param[];

extern cli_param_t tftp_ul_param[];

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       cli hook for tftp download
 * 
 * @param[in]   if_name     - if name
 * @param[in]   server_ip   - host order
 * @param[in]   filename    - file name
 */
extern void tftp_download_cli_hook(unsigned char argc, char *argv[]);

/**
 * @brief       cli hook for tftp upload
 * 
 * @param[in]   if_name     - if name
 * @param[in]   server_ip   - host order
 * @param[in]   filename    - file name
 */
extern void tftp_upload_cli_hook(unsigned char argc, char *argv[]);

/**
 * @brief       tftp module init
 */
extern void tftp_module_init();

/**
 * @brief       tftp module exit
 */
extern void tftp_module_exit();

#endif