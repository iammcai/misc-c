/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    ev_timer.h
 * @brief   用户态定时器头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-17
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-17 | cai | Initial creation.
 */

#ifndef __EV_TIMER_H__
#define __EV_TIMER_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdint.h>
#include <time.h>
#include <string.h>

#include "plat/compiler.h"
#include "plat/debug.h"
#include "type/type_heap.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 预定义定时器堆
pre_declare_heap(ev_timer)

// 定时器定义
typedef struct ev_timer_s ev_timer_t;

// 高精度定时器定义
typedef struct ev_high_res_timer_s ev_high_res_timer_t; 

// 定时器回调函数定义
typedef void (*ev_timer_cb_func)(void *args);

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       get sys time value
 * 
 * @param[out]  hour
 * @param[out]  min
 * @param[out]  sec
 * 
 * @retval      error code
 */
static attr_force_inline error_code_e realtime_get(int *hour, int *min, int *sec)
{
    pfm_ensure_ret(hour && min && sec, ERR_BAD_PARAM);

    struct timespec ts = {};
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tm_buf = {0};
    if(localtime_r(&ts.tv_sec, &tm_buf) == NULL)
        return ERR_NOT_SUPPORT;

    *hour = tm_buf.tm_hour;
    *min  = tm_buf.tm_min;
    *sec  = tm_buf.tm_sec;

    return ERR_NO_ERROR;
}

/**
 * @brief       get sys time string
 * 
 * @param[out]  buf     - string
 * @param[in]   buf_size    -  size of buf
 * 
 * @retval      error code
 */
static attr_force_inline error_code_e realtime_get_str(char *buf, size_t buf_size)
{
    // 检查buf长度，2026-01-01 23:59:59，至少需要20B
    pfm_ensure_ret(buf && buf_size >= 20, ERR_BAD_PARAM);

    memset(buf, 0, buf_size);
    struct tm tm_info = {};
    struct timespec ts = {};
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm_info);
    strftime(buf, buf_size, "%Y-%m-%d %H:%M:%S", &tm_info);

    return ERR_NO_ERROR;
}

/**
 * @brief       get sys time string, brief version
 * 
 * @param[out]  buf     - string
 * @param[in]   buf_size    -  size of buf
 * 
 * @retval      error code
 * 
 * @note        精简版本，不带年份
 */
static attr_force_inline error_code_e realtime_get_str_brief(char *buf, size_t buf_size)
{
    // "MM-DD HH:MM:SS" = 14字符 + '\0' = 15B
    pfm_ensure_ret(buf && buf_size >= 16, ERR_BAD_PARAM);

    struct timespec ts = {};
    struct tm tm_info = {};

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm_info);
    strftime(buf, buf_size, "%m-%d %H:%M:%S", &tm_info);

    return ERR_NO_ERROR;
}

/**
 * @brief`      get system mono time
 * 
 * @retval      ms
 */
extern uint64_t mono_time_get();

/**
 * @brief       get running time
 * 
 * @retval      ms
 * 
 * @note        当前运行时长
 */
extern uint64_t running_time_get();

/**
 * @brief       create an event timer
 * 
 * @param[in]   timeout     - timeout, ms
 * @param[in]   cb          - callback function
 * @param[in]   args        - args for cb
 * 
 * @retval      ptr to timer
 */
extern ev_timer_t* ev_timer_create(uint32_t timeout, ev_timer_cb_func cb, void *args);

/**
 * @brief       destroy an event timer
 * 
 * @param[in]   timer   - timer
 */
extern void ev_timer_destroy(ev_timer_t **timer);

/**
 * @brief       start an event timer
 * 
 * @param[in]   timer  - ptr to timer
 */
extern void ev_timer_start(ev_timer_t *timer);

/**
 * @brief       stop an event timer
 * 
 * @param[in]   timer  - ptr to timer
 */
extern void ev_timer_stop(ev_timer_t *timer);

/**
 * @brief       get remain time till expired
 * 
 * @param[in]   timer   - timer
 * 
 * @retval      time, ms
 * 
 * @note        获取还有多久触发定时器
 */
extern uint64_t ev_timer_remain(ev_timer_t *timer);

/**
 * @brief       create a high resolution timer
 * 
 * @param[in]   name        - hr timer name
 * @param[in]   timeout     - timeout, ms
 * @param[in]   cb          - callback function
 * @param[in]   args        - args for cb
 * 
 * @note        高精度定时器创建
 */
extern ev_high_res_timer_t* ev_high_res_timer_create(const char *name, uint64_t timeout, ev_timer_cb_func cb, void *args);

/**
 * @brief       start high resolution timer
 * 
 * @param[in]   hr_timer    - timer
 * 
 * @note        高精度定时器启动
 */
extern void ev_high_res_timer_start(ev_high_res_timer_t *hr_timer);

/**
 * @brief       stop high resolution timer
 * 
 * @param[in]   hr_timer    - timer
 * 
 * @note        高精度定时器停止
 */
extern void ev_high_res_timer_stop(ev_high_res_timer_t *hr_timer);

/**
 * @brief       init ev timer module
 */
extern void ev_timer_module_init();

#endif