/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    rpc.h
 * @brief   RPC头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-09-29
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-09-29 | cai | Initial creation.
 */

#ifndef __RPC_H__
#define __RPC_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "plat/compiler.h"
#include "plat/debug.h"
#include "proc/proc.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// shm name for rpc process
#define RPC_PROCESS_RUNNING_SHM_NAME        "/rpc_process_running_shm"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// rpc输入参数声明
typedef struct rpc_in_param_s rpc_in_param_t;

// rpc头部声明
typedef struct rpc_header_s rpc_header_t;

// rpc服务函数签名
typedef void (*rpc_server_func)(rpc_header_t *hdr, rpc_in_param_t *in_param);

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       add int type param into rpc in_param
 * 
 * @param[in]   in_param        - in params
 * @param[in]   name            - param name
 * @param[in]   val             - param value
 * 
 * @retval      error code
 */
extern error_code_e rpc_in_param_add_int(rpc_in_param_t *in_param, const char *name, int val);

/**
 * @brief       rpc call function, sync mode
 * 
 * @param[in]   dest        - dest proc id
 * @param[in]   func        - func name
 * @param[in]   in_param    - in params
 * 
 * @retval      error code
 * 
 * @note        远程异步调用
 */
extern error_code_e rpc_call_func_async(proc_id_e dest, const char* func, rpc_in_param_t *in_param);

/**
 * @brief       初始化rpc模块
 * 
 * @note        进行uss注册等，区分rpc进程和其它进程处理
 */
extern void rpc_module_init();

/**
 * @brief       rpc模块exit
 * 
 * @note        结束线程、清理资源等
 */
extern void rpc_module_exit();

/**
 * @brief       rpc process enter func
 * 
 * @note        rpc进程入口函数，作为Broker
 */
extern void rpc_proc_entry();

/**
 * @brief       注册rpc服务函数
 * 
 * @param[in]   name        - function name
 * @param[in]   func        - function
 */
extern void rpc_server_func_register(const char *name, rpc_server_func func);

#endif