/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    json_util.h
 * @brief   json工具头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-10-26
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-10-26 | cai | Initial creation.
 */

#ifndef __JSON_UTIL_H__
#define __JSON_UTIL_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "plat/compiler.h"
#include "plat/debug.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// json构建器
typedef struct json_builder_s json_builder_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

//
/**
 * @brief       create json builder
 * 
 * @retval      json builder
 */
extern json_builder_t* json_create(void);


/**
 * @brief       destroy json builder
 */
extern void json_destroy(json_builder_t *jb);

/**
 * @brief       get json string
 * 
 * @param[in]   jb
 * 
 * @retval      str
 */

extern const char* json_string_get(const json_builder_t *jb);

/**
 * @brief       get json string length
 * 
 * @param[in]   jb
 * 
 * @retval      length
 */
extern size_t json_length_get(const json_builder_t *jb);

/**
 * @brief       check if json builder has error
 * 
 * @param[in]   jb
 * 
 * @retval      1 - y, 0 -n
 */
extern int json_has_error(const json_builder_t *jb);

/**
 * @brief       json object {} start
 * 
 * @param[in]   jb
 */
extern void json_object_start(json_builder_t *jb);

/**
 * @brief       json object {} end
 * 
 * @param[in]   jb
 */
extern void json_object_end(json_builder_t *jb);

/**
 * @brief       json array [] start
 * 
 * @param[in]   jb
 */
extern void json_array_start(json_builder_t *jb);

/**
 * @brief       json arrar [] end
 * 
 * @param[in]   jb
 */
extern void json_array_end(json_builder_t *jb);

/**
 * @brief       json object start with key, e.g. key:{
 * 
 * @param[in]   jb
 * @param[in]   key     - key
 */
extern void json_object_start_with_key(json_builder_t *jb, const char *key);

/**
 * @brief       json array start with key, e.g. key: [
 */
extern void json_array_start_with_key(json_builder_t *jb, const char *key);

/**
 * @brief       json add string, e.g. key: "val"
 */
extern void json_add_string(json_builder_t *jb, const char *key, const char *val);

/**
 * @brief       json add int, e.g. key: val
 */
extern void json_add_int(json_builder_t *jb, const char *key, long long val);

/**
 * @brief       json add double, e.g. key: val
 */
extern void json_add_double(json_builder_t *jb, const char *key, double val);

/**
 * @brief       json add null, e.g. key: "null"
 */
extern void json_add_null(json_builder_t *jb, const char *key);

/**
 * @brief       json array add string, e.g. ["str1","val"]
 */
extern void json_array_add_string(json_builder_t *jb, const char *val);

/**
 * @brief       json array add int, e.g. [1,2,3]
 */
extern void json_array_add_int(json_builder_t *jb, long long val);

/**
 * @brief       json array add double, e.g. [1.1,2.2,3.3]
 */
extern void json_array_add_double(json_builder_t *jb, double val);

/**
 * @brief       json array add null string, e.g. ["null"]
 */
extern void json_array_add_null(json_builder_t *jb);

#endif