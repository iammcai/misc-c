/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    http.c
 * @brief   http协议相关实现
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

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <string.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/sendfile.h>       // for sendfile

#include "web/http.h"
#include "mp/mp.h"
#include "event/ev_timer.h"
#include "led/led.h"
#include "sht30/sht30.h"
#include "sysmon/sysmon.h"
#include "zcap/zcap.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// http uri路径最大长度
#define HTTP_PATH_MAX_LEN   (128)

// header长度
#define HTTP_HEADER_BUFFER_SIZE (1024)

// body长度
#define HTTP_BODY_BUFFER_SIZE   (2048)

// 模块单独CGI响应body长度
#define HTTP_CGI_BODY_BUFFER_SIZE   (256)

// http资源目录
#define HTTP_RESOURCE_PREFIX        "../mgnt/web/resource"

// debug开关
#define HTTP_DEBUG_ON       (0)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 文件类型条目
typedef struct{
    const char *ext;        // 拓展名
    const char *type;       // 响应类型
}http_mime_entry_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       find position of \r\n
 * 
 * @param[in]   buf     - buffer
 * @param[in]   len     - length of buf
 * 
 * @retval      ptr to \r
 * 
 * @note        查找[buf, buf+len)范围内的\r\n，返回\r指针
 */
static attr_pure_inline const char* _http_find_crlf(const char *buf, size_t len)
{
    if(len < 2)
        return NULL;
    for(size_t i = 0; i < len - 1; ++ i)
        if(buf[i] == '\r' && buf[i+1] == '\n')
            return &buf[i];
    return NULL;
}

/**
 * @brief       parse uint8 number, if fail, return 0 and sign err
 * 
 * @param[in]   str     - str
 * @param[in]   len     - str len
 * 
 * @param[out]  err     - if parse fail, 1
 * 
 * @retval      parse result
 */
static attr_force_inline size_t _http_parse_uint(const char *str, size_t len, int *err)
{
    size_t val = 0;
    *err = 0;

    for(size_t i = 0; i < len; ++ i)
    {
        if(str[i] < '0' || str[i] > '9')
        {
            *err = 1;
            return 0;
        }
        size_t new_val = val * 10 + (str[i] - '0');
        if(new_val < val)       // 溢出了
        {
            *err = 1;
            return 0;
        }
        val = new_val;
    }

    return val;
}

/**
 * @brief       get mime type
 * 
 * @param[in]   path    - uri path
 * 
 * @retval      mime type
 */
static attr_pure_inline const char* _http_get_mime_type(const char *path);

/**
 * @brief       get cgi response body
 * 
 * @param[in]   uri     - uri
 * @param[in]   buf     - json buffer
 * @param[in]   buf_size    - size of buffer
 * 
 * @retval      error code
 */
static attr_pure_inline error_code_e _http_get_cgi_response(const char *uri, char *buf, size_t buf_size);

/**
 * @brief       send http error response
 * 
 * @param[in]   fd      - fd
 * @param[in]   code    - error code
 * @param[in]   reason  - error reason
 */
static void _http_send_error_response(int fd, int code, const char *reason);

/**
 * @brief       http response for cgi: /api/sys/realtime
 */
static attr_force_inline error_code_e sys_realtime_http_cgi_cb(char *buf, size_t buf_size)
{
    char time_str[32] = {}; // 32长度足够

    if(ERR_NO_ERROR != realtime_get_str(time_str, sizeof(time_str)))
        return ERR_NOT_SUPPORT;

    snprintf(buf, buf_size, "{\"realtime\":\"%s\"}", time_str);

    return ERR_NO_ERROR;
}

/**
 * @brief       http response for cgi: /api/sync
 * 
 * @note        web端定时刷新接口，需要收集所有模块数据上报
 */
static attr_force_inline error_code_e _http_sync_http_cgi_cb(char *buf, size_t buf_size)
{
    memset(buf, 0, buf_size);

    char led_status[HTTP_CGI_BODY_BUFFER_SIZE] = {};
    pfm_ensure_ret(ERR_NO_ERROR == led_status_http_cgi_cb(led_status, HTTP_CGI_BODY_BUFFER_SIZE), ERR_WEB_HTTP_CGI_RESP_ERROR);
    char sht30_sensor[HTTP_CGI_BODY_BUFFER_SIZE] = {};
    pfm_ensure_ret(ERR_NO_ERROR == sht30_sensor_http_cgi_cb(sht30_sensor, HTTP_CGI_BODY_BUFFER_SIZE), ERR_WEB_HTTP_CGI_RESP_ERROR);
    char sysmon_cpu_usage[HTTP_CGI_BODY_BUFFER_SIZE] = {};
    pfm_ensure_ret(ERR_NO_ERROR == sysmon_cpu_usage_http_cgi_cb(sysmon_cpu_usage, HTTP_CGI_BODY_BUFFER_SIZE), ERR_WEB_HTTP_CGI_RESP_ERROR);
    char network_status_rxrate[HTTP_CGI_BODY_BUFFER_SIZE] = {};
    pfm_ensure_ret(ERR_NO_ERROR == zcap_rx_rate_http_cgi_cb(network_status_rxrate, HTTP_CGI_BODY_BUFFER_SIZE), ERR_WEB_HTTP_CGI_RESP_ERROR);

    int n = snprintf(buf, buf_size,
        "{\"led\":%s,\"sht30\":%s,\"sysmon\":%s,\"network\":%s}",
        led_status, sht30_sensor, sysmon_cpu_usage, network_status_rxrate);
    if(unlikely(n >= buf_size))
        return ERR_WEB_HTTP_CGI_RESP_ERROR;

    return ERR_NO_ERROR;
}

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// mime表项
static http_mime_entry_t g_http_mime_table[] = {
    {".html",   "text/html; charset=utf-8"},
    {".htm",    "text/html; charset=utf-8"},
    {".css",    "text/css; charset=utf-8"},
    {".js",     "application/javascript; charset=utf-8"},
    // TODO: 可扩展
};

// cgi响应接口表项
static http_cgi_entry_t g_http_cgi_response_table[] = {
    {"/api/sync",                       _http_sync_http_cgi_cb},
    {"/api/sys/realtime",               sys_realtime_http_cgi_cb},
    {"/api/led/turn",                   led_turn_http_cgi_cb},
    {"/api/led/status",                 led_status_http_cgi_cb},
    {"/api/sht30/sensor",               sht30_sensor_http_cgi_cb},
    {"/api/sysmon/cpu/usage",           sysmon_cpu_usage_http_cgi_cb},
    {"/api/network/status/rx_rate",     zcap_rx_rate_http_cgi_cb},
};

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static inline const char* _http_get_mime_type(const char *path)
{
    const char *dot = strchr(path, '.');
    if(!dot)
        return "application/octet-stream";

    size_t count = array_size(g_http_mime_table);
    for(size_t i = 0; i < count; ++ i)
        if(strcasecmp(dot, g_http_mime_table[i].ext) == 0)
            return g_http_mime_table[i].type;

    return "application/octet-stream";
}

static inline error_code_e _http_get_cgi_response(const char *uri, char *buf, size_t buf_size)
{
    dbg_major("receive uri %s", uri);

    for(size_t i = 0; i < array_size(g_http_cgi_response_table); ++ i)
        if(0 == strcasecmp(g_http_cgi_response_table[i].uri, uri))
            if(g_http_cgi_response_table[i].func)
                return g_http_cgi_response_table[i].func(buf, buf_size);
            else
                return ERR_WEB_HTTP_CGI_NOT_FOUND;
    return ERR_WEB_HTTP_CGI_NOT_FOUND;
}

static void _http_send_error_response(int fd, int code, const char *reason)
{
    // body
    char body[HTTP_BODY_BUFFER_SIZE] = {};
    int body_len = snprintf(body, sizeof(body),
        "<html><head><title>%d %s</title></head>"
        "<body><h1>%d %s</h1></body></html>",
        code, reason, code, reason);
    // header
    char header[HTTP_HEADER_BUFFER_SIZE];
    int hdr_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        code, reason, body_len);
    
    // header和body一起发送
    char buffer[HTTP_BODY_BUFFER_SIZE] = {};
    int total_len = body_len + hdr_len;
    
    memcpy(buffer, header, hdr_len);
    memcpy(buffer + hdr_len, body, body_len);

    ssize_t written = 0;
    while(written < total_len)
    {
        ssize_t w = write(fd, buffer + written, total_len - written);
        if(w <= 0)
        {
            dbg_error("write() fail");
            return;
        }
        written += w;
    }
}

http_parse_result_e http_parse_request(const char *buffer, size_t buffer_len, http_request_t *req, size_t *consumed)
{
    const char *pos = buffer;
    const char *end = buffer + buffer_len;
    *consumed = 0;

#if HTTP_DEBUG_ON
    char *debug_buffer = mp_calloc(buffer_len+1, sizeof(char));
    memcpy(debug_buffer, buffer, buffer_len);
    debug_buffer[buffer_len] = '\0';
    dbg_always("parse buffer:\n%s\n", debug_buffer);
    mp_free(debug_buffer, sizeof(char)*(buffer_len+1));
#endif

    /* =-=-=-=-=-=-=-=- 解析请求行 -=-=-=-=-=-=-=-= */

    const char *crlf = _http_find_crlf(pos, end-pos);
    if(!crlf)
        return HTTP_PARSE_COMPLETE;

    // 请求行格式：method uri version
    const char *sp1 = memchr(pos, ' ', crlf-pos);
    if(!sp1)
        return HTTP_PARSE_ERROR;
    const char *sp2 = memchr(sp1 + 1, ' ', crlf-sp1-1);
    if(!sp2)
        return HTTP_PARSE_ERROR;

    req->method.ptr = pos;
    req->method.len = sp1-pos;
    if(req->method.len == 0)
        return HTTP_PARSE_ERROR;

    req->uri.ptr = sp1+1;
    req->uri.len = sp2-sp1-1;
    if(req->uri.len == 0)
        return HTTP_PARSE_ERROR;

    req->version.ptr = sp2+1;
    req->version.len = crlf-sp2-1;
    if(req->version.len == 0)
        return HTTP_PARSE_ERROR;

    pos = crlf + 2;

    /* =-=-=-=-=-=-=-=- 解析请求headers -=-=-=-=-=-=-=-= */

    while(1)
    {
        crlf = _http_find_crlf(pos, end - pos);
        if(!crlf)
            return HTTP_PARSE_INCOMPLETE;

        if(crlf == pos)     // 空行表示解析完毕
        {
            pos += 2;
            break;
        }

        if(req->header_count >= HTTP_HEADER_MAX_CNT)
            return HTTP_PARSE_ERROR;

        // header格式：name: value，可能有空格\t
        const char *colon = memchr(pos, ':', crlf - pos);
        if(!colon)
            return HTTP_PARSE_ERROR;
        size_t index = req->header_count ++;
        req->headers[index].name.ptr = pos;
        req->headers[index].name.len = colon-pos;
        // 提取value，去除前导空格和后续所有空格
        const char *value_start = colon+1;
        while(value_start < crlf && (*value_start == ' ' || *value_start == '\t'))
            value_start++;
        const char *value_end = crlf;
        while(value_end > value_start && (*(value_end-1) == ' ' || *(value_end-1) == '\t'))
            value_end--;
        req->headers[index].value.ptr = value_start;
        req->headers[index].value.len = value_end - value_start;

        // 提取Content-Length属性
        if(req->headers[index].name.len == 14 && 0 == strncasecmp(req->headers[index].name.ptr, "Content-Length", 14))
        {
            int err = 0;
            req->content_len = _http_parse_uint(req->headers[index].value.ptr, req->headers[index].value.len, &err);
            if(err)
                return HTTP_PARSE_ERROR;
            req->has_content_len = 1;
        }

        pos = crlf + 2;
    }

    /* =-=-=-=-=-=-=-=- 解析body -=-=-=-=-=-=-=-= */

    if(req->has_content_len)
    {
        size_t remaining = end - pos;
        if(remaining < req->content_len)        // 存在拆包
            return HTTP_PARSE_INCOMPLETE;
        req->body.ptr = pos;
        req->body.len = req->content_len;
        pos += req->content_len;
    }
    else
    {
        req->body.ptr = NULL;
        req->body.len = 0;
    }

    *consumed = pos - buffer;
    return HTTP_PARSE_COMPLETE;
}

void http_send_file_response(int fd, const char *uri)
{

    /* =-=-=-=-=-=-=-=- 路径拼接校验 -=-=-=-=-=-=-=-= */

    char filepath[HTTP_PATH_MAX_LEN] = {};
    // 检查是否"/"开头
    if(strncmp(uri, "/", 1) != 0)
        goto bad_request;

    // 拼接路径
    if(strlen(uri) == 1)
        snprintf(filepath, HTTP_PATH_MAX_LEN, "%s/index.html", HTTP_RESOURCE_PREFIX);
    else
        snprintf(filepath, HTTP_PATH_MAX_LEN, "%s%s", HTTP_RESOURCE_PREFIX, uri);
    // 检查真实路径在resource目录下
    char resolved[HTTP_PATH_MAX_LEN] = {};
    if(NULL == realpath(filepath, resolved))
    {
        if(errno == ENOENT)
            goto not_found;
        goto forbidden;
    }
    char resolved_prefix[HTTP_PATH_MAX_LEN] = {};
    if(NULL == realpath(HTTP_RESOURCE_PREFIX, resolved_prefix))
        goto internal_error;
    if((strncmp(resolved, resolved_prefix, strlen(resolved_prefix)) != 0) ||
        (resolved[strlen(resolved_prefix)] != '/' && resolved[strlen(resolved_prefix)] != '\0'))
        goto forbidden;
    
    /* =-=-=-=-=-=-=-=- 获取文件信息 -=-=-=-=-=-=-=-= */

    // 检查文件能否打开
    struct stat st = {};
    if(stat(resolved, &st) < 0)
    {
        if(errno == ENOENT)
            goto not_found;
        goto forbidden;
    }
    // 检查确实是普通文件
    if(!S_ISREG(st.st_mode))
        goto forbidden;

    /* =-=-=-=-=-=-=-=- 打开文件并发送 -=-=-=-=-=-=-=-= */

    int file_fd = open(resolved, O_RDONLY);
    if(file_fd < 0)
        goto internal_error;

    const char *mime = _http_get_mime_type(resolved);
    char header_buf[HTTP_HEADER_BUFFER_SIZE] = {};
    int hdr_len = snprintf(header_buf, sizeof(header_buf),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: keep-alive\r\n"
        "Server: TinyCServer/1.0\r\n"
        "\r\n",
        mime, (long)st.st_size);
    // 循环写，确保头部完整发出
    ssize_t written = 0;
    while(written < hdr_len)
    {
        ssize_t w = write(fd, header_buf + written, hdr_len - written);
        if(w <= 0)
        {
            dbg_error("write() header fail");
            close(file_fd);
            return;
        }
        written += w;
    }
    // 发送文件本体
    off_t offset = 0;
    while(offset < st.st_size)
    {
        ssize_t send = sendfile(fd, file_fd, &offset, st.st_size - offset);
        if(send <= 0)
        {
            if(errno == EAGAIN || errno == EINTR)
                continue;
            dbg_error("sendfile() fail");
            close(file_fd);
            return;
        }
    }
    // 关闭文件
    close(file_fd);

    dbg("send uri %s ok", uri);

    return;

bad_request:
    _http_send_error_response(fd, 400, "Bad Request");
    return;
not_found:
    _http_send_error_response(fd, 404, "Not Found");
    return;
forbidden:
    _http_send_error_response(fd, 403, "Forbidden");
    return;
internal_error:
    _http_send_error_response(fd, 500, "Internal Server Error");
    return;
}

void http_send_cgi_response(int fd, const char *uri)
{
    // TODO: 实现
    dbg("respone cgi %s for fd %d", uri, fd);

    /* =-=-=-=-=-=-=-=- 获取cgi响应体 -=-=-=-=-=-=-=-= */

    char body[HTTP_BODY_BUFFER_SIZE] = {};
    error_code_e ret = _http_get_cgi_response(uri, body, HTTP_BODY_BUFFER_SIZE);
    if(ERR_NO_ERROR != ret)
        goto not_found;

    /* =-=-=-=-=-=-=-=- 组装响应 -=-=-=-=-=-=-=-= */

    char buffer[HTTP_BODY_BUFFER_SIZE] = {};
    int len = snprintf(buffer, sizeof(buffer),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: %ld\r\n"
        "Connection: keep-alive\r\n"
        "Cache-Control: no-cache\r\n"
        "\r\n"
        "%s",
        (long)strlen(body), body);

    /* =-=-=-=-=-=-=-=- 发送 -=-=-=-=-=-=-=-= */

    ssize_t written = 0;
    while(written < len)
    {
        ssize_t w = write(fd, buffer + written, len - written);
        if(w <= 0)
        {
            dbg_error("write() fail");
            return;
        }
        written += w;
    }

    return;

not_found:
    _http_send_error_response(fd, 404, "Not Found");
    return;
}