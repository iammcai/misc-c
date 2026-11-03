/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    sht30.h
 * @brief   sht30用户态头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-07-31
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-07-31 | cai | Initial creation.
 */

#ifndef __SHT30_H__
#define __SHT30_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "plat/compiler.h"
#include "plat/debug.h"
#include "json_util/json_util.h"
#include "cli/cli.h"

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

typedef struct cli_ctx_s cli_ctx_t;

/**
 * @brief       cli hook for: show temperature
 */
extern void sht30_show_temp(cli_ctx_t *ctx, unsigned char argc, char *argv[]);

/**
 * @brief       cli hook for: show humidity
 */
extern void sht30_show_humi(cli_ctx_t *ctx, unsigned char argc, char *argv[]);

/**
 * @brief       cli hook for: show temperature history
 */
extern void sht30_show_temp_history(cli_ctx_t *ctx, unsigned char argc, char *argv[]);

/**
 * @brief       cli hook for: show humidity history
 */
extern void sht30_show_humi_history(cli_ctx_t *ctx, unsigned char argc, char *argv[]);

/**
 * @brief       cb for http cgi: /api/sht30/sensor
 */
extern error_code_e sht30_sensor_http_cgi_cb(json_builder_t *jb);

/**
 * @brief       cb for http cgi: /api/sht30/history
 */
extern error_code_e sht30_history_http_cgi_cb(json_builder_t *jb);

/**
 * @brief       cb for http cgi: /api/sync sht30 module
 * 
 * @note        不含{}版本
 */
extern error_code_e sht30_sync_http_cgi_cb(json_builder_t *jb);

/**
 * @brief       get temperature form sht30
 * 
 * @param[out]  temp
 * 
 * @retval      error code
 */
extern error_code_e sht30_read_temperature(double *temp);

/**
 * @brief       get humidity form sht30
 * 
 * @param[out]  humi
 * 
 * @retval      error code
 */
extern error_code_e sht30_read_humidity(double *humi);

/**
 * @brief       init sht30 module
 */
extern void sht30_module_init();

/**
 * @brief       exit sht30 module
 */
extern void sht30_module_exit();

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

#endif