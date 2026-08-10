/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    http.h
 * @brief   http协议解析头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-08-10
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-08-10 | cai | Initial creation.
 */

#ifndef __HTTP_H__
#define __HTTP_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdio.h>
#include "plat/compiler.h"
#include "plat/debug.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 最大header数量
#define HTTP_HEADER_MAX_CNT     (64)

// uri长度
#define HTTP_URI_SIZE           (64)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 解析结果枚举定义
typedef enum{
    HTTP_PARSE_ERROR = -1,          // 错误
    HTTP_PARSE_COMPLETE = 0,        // 解析成功
    HTTP_PARSE_INCOMPLETE,          // 数据不完整，需要继续读取
}http_parse_result_e;

// hhtp str
typedef struct{
    const char *ptr;    // 指向缓冲区对应内容的起始
    size_t len;         // 长度
}http_str_t;

// http请求定义
typedef struct{
    http_str_t method;
    http_str_t uri;
    http_str_t version;

    struct{
        http_str_t name;
        http_str_t value;
    }headers [HTTP_HEADER_MAX_CNT];
    size_t header_count;

    http_str_t body;
    size_t content_len;
    char has_content_len;
}http_request_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       parse http request
 * 
 * @param[in]   buffer      - 缓冲区头部，http内容
 * @param[in]   buffer_len  - 缓冲长度
 * @param[out]  req         - 解析结果
 * @param[in]   consumed    - 本次解析消耗字节数
 * 
 * @retval      parse result
 */
extern http_parse_result_e http_parse_request(const char *buffer, size_t buffer_len, http_request_t *req, size_t *consumed);

/**
 * @brief       send file response
 * 
 * @param[in]   fd      - fd
 * @param[in]   uri     - uri
 * 
 * @note        发送静态文件响应
 */
extern void http_send_file_response(int fd, const char *uri);

#endif