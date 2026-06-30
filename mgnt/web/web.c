/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    web.c
 * @brief   web管理实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-29
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-29 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdio.h>
#include <sys/stat.h>       // for stat
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>     // for socket
#include <netinet/in.h>     // for struct sockaddr_in
#include <fcntl.h>          // for fcntl
#include "web/web.h"
#include "event/ev_loop.h"
#include "msg_q/msg_q.h"
#include "event/ev_thread.h"
#include "plat/debug.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// web服务使用的端口号
#define WEB_PORT        (8080)
// 最大连接数量
#define WEB_CONN_MAX    (16)
// 消息队列长度
#define WEB_MSGQ_SIZE   (WEB_CONN_MAX * 16)
// 缓冲区大小
#define WEB_BUFFER_SIZE (4096)

/* web字符串长度 */
#define WEB_BUFFER_SIZE_256     (256)
#define WEB_BUFFER_SIZE_512     (512)
#define WEB_BUFFER_SIZE_4096    (4096)

// 状态字符串
// 错误请求
#define WEB_STATUS_BAD_REQ                  "Bad Request"
#define WEB_STATUS_FORBIDDEN                "Forbidden"
#define WEB_STATUS_NOT_FOUND                "Not Found"
#define WEB_STATUS_METHOD_NOT_ALLOWED       "Method Not Allowed"

#define WEB_DOCS_ROOT   "../mgnt/web/source"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// web管理结构定义
typedef struct{
    int sock_fd;        // 用于监听连接用的tcp socket fd
    char buffer[WEB_BUFFER_SIZE];   // 缓冲区，接收客户端使用
}web_t;

// web状态码枚举
typedef enum{
    WEB_STATUS_CODE_BAD_REQUEST = 400,
    WEB_STATUS_CODE_FORBIDDEN = 403,
    WEB_STATUS_CODE_NOT_FOUND = 404,
    WEB_STATUS_CODE_METHOD_NOT_ALLOWED = 405
}web_status_code_e;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       web evloop callback
 * 
 * @note        web事件注册到ev_loop指定的回调，通知主线程处理
 */
static attr_force_inline void _web_el_cb(int fd, void *args);

/**
 * @brief       web handle new connection
 * 
 * @note        处理新的web连接请求
 */
static attr_force_inline void _web_handle_new_conn();

/**
 * @brief       web handle extern msg
 * 
 * @param[in]   fd
 * 
 * @note        处理已连接的客户端消息
 */
static void _web_handle_msg(int fd);

/**
 * @brief       send error to client
 * 
 * @param[in]   fd  - conn fd
 * @param[in]   sc  - status code
 * @param[in]   status  - status string
 */
static void _web_send_error(int fd, web_status_code_e sc, const char *status);

/**
 * @brief       web handle get file request
 * 
 * @param[in]   fd      - conn fd
 * @param[in]   path    - path in req head
 * 
 * @note        处理GET请求，响应相关PATH 
 */
static void _web_handle_get(int fd, const char *path);

/**
 * @brief       work func of web ev_thd
 * 
 * @note        处理web事件的工作函数
 */
static void _web_ev_thd_work(void *args);

/**
 * @brief       ctor init web module
 * 
 * @note        构造初始化web，启动服务
 */
static void web_early_init() attr_ctor(CTOR_PRIO_LOW);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 全局web服务管理变量
static web_t g_web_mgnt = {};
// 处理web事件的线程
declare_ev_thd(web, _web_ev_thd_work, NULL, -1);
// msgq，用于ev_loop和web之间
declare_msg_q(web, WEB_MSGQ_SIZE, sizeof(int))

// 发送Head缓冲区
static char head_buf[WEB_BUFFER_SIZE_512] = {};
// 发送内容缓冲区
static char send_buf[WEB_BUFFER_SIZE_4096] = {};

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static inline void _web_el_cb(int fd, void *args)
{
    // 往msgq推送fd信息
    msg_q_push(web, &fd, sizeof(int));
    // 唤醒线程进行处理
    ev_thd_wake(web);
}

static void _web_ev_thd_work(void *args)
{
    // 一次性处理完msg
    int fd = -1;
    while(msg_q_ret_ok == msg_q_pop(web, sizeof(int), msg_q_no_wait, &fd))
    {
        if(fd == g_web_mgnt.sock_fd)    // 新的连接
            _web_handle_new_conn();
        else
            _web_handle_msg(fd);
    }
}

static inline void _web_handle_new_conn()
{
    dbg("handle new connection");
    
    struct sockaddr_in client_addr = {};
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(g_web_mgnt.sock_fd, (struct sockaddr*)&client_addr, &addr_len);
    pfm_ensure_ret_void(client_fd >= 0);

    // 将新的fd注册到evloop中
    event_loop_register_file_event(client_fd, EL_FILE_EVENT_READABLE, _web_el_cb, NULL);
}

static void _web_handle_msg(int fd)
{
    dbg("handle client msg, fd %d", fd);

    char *buffer = g_web_mgnt.buffer;

    memset(buffer, 0, WEB_BUFFER_SIZE);

    ssize_t n = recv(fd, buffer, WEB_BUFFER_SIZE-1, 0);
    if(n < 0)   // 处理错误
    {
        if(errno == EAGAIN || errno == EWOULDBLOCK) // 数据未就绪，等待下次处理
            return;
        else
        {
            dbg_error("recv error on fd %d, error: %s", fd, strerror(errno));
            goto clean_up;
        }
    }
    else if(n == 0)     // 客户端主动关闭
    {
        dbg_major("client on fd %d close conn", fd);
        goto clean_up;
    }

    buffer[n] = '\0';   // 添加结束符

    // 解析请求头部，格式固定。这里使用魔数
    char method[16] = {}, path[256] = {}, version[16] = {};
    if(3 != sscanf(buffer, "%15s %255s %15s", method, path, version))
    {
        dbg_error("bad request");
        _web_send_error(fd,  WEB_STATUS_CODE_BAD_REQUEST, WEB_STATUS_BAD_REQ);
        goto clean_up;
    }

    // 仅支持GET
    if(0 != strcasecmp(method, "GET"))
    {
        dbg_error("method not allowed");
        _web_send_error(fd,  WEB_STATUS_CODE_METHOD_NOT_ALLOWED, WEB_STATUS_METHOD_NOT_ALLOWED);
        goto clean_up;
    }

    // 处理文件请求
    _web_handle_get(fd, path);

    //dbg_always("handle done");
    // 处理完毕关闭，短链接

clean_up:
    // 从ev_loop注销事件，关闭socket
    event_loop_deregister_file_event(fd);
    close(fd);
}

static void _web_send_error(int fd, web_status_code_e sc, const char *status)
{
    char body[WEB_BUFFER_SIZE_256] = {};
    char resp[WEB_BUFFER_SIZE_512] = {};

    snprintf(body, sizeof(body), 
        "<html><body><h1>%d %s</h1></body></html>\r\n", sc, status
    );

    snprintf(resp, sizeof(resp),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             sc, status, strlen(body), body);

    ssize_t n = send(fd, resp, strlen(resp), 0);
    pfm_ensure_dbg(n == strlen(resp));
}

static void _web_handle_get(int fd, const char *path)
{
    char path_copy[WEB_BUFFER_SIZE_256] = {};
    char full_path[WEB_BUFFER_SIZE_256] = {};

    // 复制path，剥离?之后的查询字符串
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    char *q_mark = strchr(path_copy, '?');
    if(q_mark)
        *q_mark = '\0';
    
    // 处理根路径
    if(path_copy[0] == '\0' || (path_copy[0] == '/' && path_copy[1] == '\0'))
        strcpy(path_copy, "/index.html");
    
    // 禁止..
    if(strstr(path_copy, ".."))
    {
        _web_send_error(fd, WEB_STATUS_CODE_FORBIDDEN, WEB_STATUS_FORBIDDEN);
        return;
    }

    // 构造完整路径
    snprintf(full_path, sizeof(full_path), "%s%s", WEB_DOCS_ROOT, path_copy);

    // 检查文件是否存在并且普通文件
    struct stat st;
    if(stat(full_path, &st) != 0 || !S_ISREG(st.st_mode))
    {
        dbg_error("find file: %s fail", full_path);
        _web_send_error(fd, WEB_STATUS_CODE_NOT_FOUND, WEB_STATUS_NOT_FOUND);
        return;
    }

    // 根据扩展名决定 Content-Type
    const char *content_type = "application/octet-stream";
    const char *ext = strrchr(full_path, '.');
    if(ext) 
    {
        if(strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0)
            content_type = "text/html";
        else if(strcasecmp(ext, ".css") == 0)
            content_type = "text/css";
        else if(strcasecmp(ext, ".js") == 0)
            content_type = "application/javascript";
        else if(strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)
            content_type = "image/jpeg";
        else if(strcasecmp(ext, ".png") == 0)
            content_type = "image/png";
        else if(strcasecmp(ext, ".gif") == 0)
            content_type = "image/gif";
        else if(strcasecmp(ext, ".txt") == 0)
            content_type = "text/plain";
    }

    // 7. 构造响应头
    memset(head_buf, 0, sizeof(head_buf));
    int head_len = snprintf(head_buf, sizeof(head_buf),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lld\r\n"
        "Connection: close\r\n"
        "\r\n",
        content_type, (long long)st.st_size);

    // 发送响应头
    if(send(fd, head_buf, head_len, 0) != head_len)
    {
        dbg_error("send response header failed for fd %d", fd);
        return;
    }

    // 打开文件发送内容
    FILE *fp = fopen(full_path, "rb");
    if(!fp)
    {
        dbg_error("open file: %s fail", full_path);
        _web_send_error(fd, WEB_STATUS_CODE_NOT_FOUND, WEB_STATUS_NOT_FOUND);
        return;
    }
    memset(send_buf, 0, sizeof(send_buf));
    size_t bytes_read;
    while((bytes_read = fread(send_buf, 1, sizeof(send_buf), fp)) > 0)
    {
        ssize_t sent = send(fd, send_buf, bytes_read, 0);
        if(sent != (ssize_t)bytes_read)
        {
            dbg_error("send file content failed for fd %d", fd);
            break;
        }
    }
    fclose(fp);

    //dbg_always("file %s sent to fd %d", full_path, fd);
}

static void web_early_init()
{
    // 启动解析线程
    ev_thd_run(web)

    // 创建socket
    g_web_mgnt.sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    pfm_ensure_done(g_web_mgnt.sock_fd >= 0);

    // 设置地址复用
    int opt = 1;
    pfm_ensure_dbg(0 == setsockopt(g_web_mgnt.sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)));

    // socket绑定地址：any:8080
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(WEB_PORT),
        .sin_addr = INADDR_ANY,
    };
    pfm_ensure_done(0 == bind(g_web_mgnt.sock_fd, (struct sockaddr*)&addr, sizeof(addr)));
    // 开启监听
    pfm_ensure_done(0 == listen(g_web_mgnt.sock_fd, WEB_CONN_MAX));
    // 设置非阻塞
    int flags = fcntl(g_web_mgnt.sock_fd, F_GETFL, 0);
    fcntl(g_web_mgnt.sock_fd, flags | O_NONBLOCK);

    // 注册到ev_loop
    event_loop_register_file_event(g_web_mgnt.sock_fd, EL_FILE_EVENT_READABLE, _web_el_cb, NULL);

    dbg_major("web init ok");

    return;

done:
    if(g_web_mgnt.sock_fd >= 0)
        close(g_web_mgnt.sock_fd);
    
    memset(&g_web_mgnt, 0, sizeof(web_t));
}