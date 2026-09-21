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

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       cli hook for: show temperature
 */
extern void sht30_show_temp(unsigned char argc, char *argv[]);

/**
 * @brief       cli hook for: show humidity
 */
extern void sht30_show_humi(unsigned char argc, char *argv[]);

/**
 * @brief       cb for http cgi: /api/sht30/sensor
 * 
 * @retval      json for temp and humi
 */
extern error_code_e sht30_sensor_http_cgi_cb(char *buf, size_t buf_size);

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
 * @brief       deinit sht30 module
 */
extern void sht30_module_deinit();

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

#endif