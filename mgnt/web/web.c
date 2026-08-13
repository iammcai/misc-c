/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    web.c
 * @brief   web服务实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-08-07
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-08-07 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>     // for socket api
#include <sys/epoll.h>      // for epoll api
#include <netinet/in.h>     // for struct sockaddr_in
#include <pthread.h>
#include <sys/errno.h>
#include <stdio.h>

#include "web/web.h"
#include "plat/atom.h"
#include "type/type_list.h"
#include "mp/mp.h"
#include "zcap/zcap.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// web服务器监听端口
#define WEB_TCP_PORT        (8080)

// 最大连接数量
#define WEB_CONNECT_MAX_CNT     (128)

// epoll等待时间
#define WEB_EPOLL_WAIT_MS       (1000)

// web缓冲区长度
#define WEB_BUFFER_SIZE         (4096)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 预定义链表，用于管理connection
pre_declare_list(web_connection)

// 连接持久化数据结构
typedef struct{
    int fd;                             // 对应fd
    char buffer[WEB_BUFFER_SIZE];       // 缓冲区
    size_t len;                         // 当前有效缓冲区长度

    web_connection_list_item_t item;    // list item
}web_connection_t;

// web服务器管理结构定义
typedef struct{
    int sock_fd;                // 监听连接的socket fd
    int epoll_fd;               // epoll_fd
    pthread_t tid;              // 运行epoll等待的线程ID

    ATOMIC_UINT8_T running;     // 服务器运行标志
}web_mgnt_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

// 定义链表操作
declare_list(web_connection, web_connection, web_connection_t, item)

/**
 * @brief       find web connection by fd
 * 
 * @param[in]   fd      - connection fd
 * 
 * @retval      search result
 */
static attr_force_inline web_connection_t* _web_connection_find(int fd);

/**
 * @brief       web process http request
 * 
 * @param[in]   fd      - fd
 * @param[in]   req     - request
 * 
 * @note        处理解析好的http请求，响应
 */
static void _web_process_http_req(int fd, http_request_t *req);

/**
 * @brief       handle web events
 * 
 * @param[in]   fd      - fd ready
 */
static void _web_events_handle(int fd);

/**
 * @brief       web epoll routine
 */
static attr_force_inline void* _web_epoll_routine(void *args);

/**
 * @brief       init web mgnter
 * 
 * @retval      error code
 * 
 * @note        初始化web连接需要的资源 socket epoll等
 */
static error_code_e _web_mgnt_init(void);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// fixed内存池，用于connection
declare_mem_type_fixed(web_connection, sizeof(web_connection_t), WEB_CONNECT_MAX_CNT)

// 全局web connection链表
static web_connection_list_head_t g_web_conn_list = {};

// 全局唯一管理结构
static web_mgnt_t g_web_mgnter = {};

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static attr_force_inline web_connection_t* _web_connection_find(int fd)
{
    web_connection_t *conn = web_connection_list_first(&g_web_conn_list);
    while(conn)
    {
        if(conn->fd == fd)
            return conn;
        conn = web_connection_list_next(conn);
    }

    return NULL;
}

static void _web_process_http_req(int fd, http_request_t *req)
{
    char uri[HTTP_URI_SIZE] = {};

    // 处理GET方法
    if(0 == strncasecmp(req->method.ptr, "GET", req->method.len))
    {
        memcpy(uri, req->uri.ptr, req->uri.len);
        http_send_file_response(fd, uri);
    }
    else if(0 == strncasecmp(req->method.ptr, "POST", req->method.len))     // post方法
    {
        memcpy(uri, req->uri.ptr, req->uri.len);
        http_send_cgi_response(fd, uri);
    }
}

static void _web_events_handle(int fd)
{
    if(fd == g_web_mgnter.sock_fd)      // 新连接请求
    {
        // accept
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int fd_new = accept(g_web_mgnter.sock_fd, (struct sockaddr*)&client_addr, &client_len);
        pfm_ensure_ret_void(fd_new >= 0);

        // 将socket设置为非阻塞
        int flags = fcntl(fd_new, F_GETFL, 0);
        fcntl(fd_new, F_SETFL, flags | O_NONBLOCK);

        // 加入到epoll
        struct epoll_event ev = {.data.fd = fd_new, .events = EPOLLIN | EPOLLET};
        if(0 != epoll_ctl(g_web_mgnter.epoll_fd, EPOLL_CTL_ADD, fd_new, &ev))
        {
            dbg_error("epoll_ctl add fail");
            close(fd_new);
            return;
        }

        // 获取connection内存，添加到链表记录
        web_connection_t *conn = (web_connection_t*)mp_fixed_node_get(web_connection);
        if(!conn)
        {
            dbg_error("get memory for connection fail");
            close(fd_new);
            return;
        }
        memset(conn, 0, sizeof(web_connection_t));
        conn->fd = fd_new;
        web_connection_list_add_head(&g_web_conn_list, conn);

        dbg("handle new connection, fd %d", fd_new);
    }
    else
    {
        dbg("handle msg from fd %d", fd);
        // 找到对应fd的connection
        web_connection_t *conn = _web_connection_find(fd);
        if(!conn)
        {
            dbg_error("find web connection for fd %d fail", fd);
            return;
        }
        // 将数据读取完
        while(1)
        {
            ssize_t n = recv(conn->fd, conn->buffer + conn->len, WEB_BUFFER_SIZE - conn->len, 0);
            if(n > 0)       // 读取到内容
            {
                conn->len += n;
                if(conn->len == WEB_BUFFER_SIZE)        // 缓冲区满，退出
                    break;
            }
            else if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) // 内核缓冲区读取完毕
                break;
            else            // 对端关闭或者出错
            {
                close(fd);      // 内含epoll_del
                web_connection_list_del(&g_web_conn_list, conn);
                mp_fixed_node_put(conn);        // 归还内存
                return;
            }
        }

        // 解析，需要处理粘包和拆包
        size_t rpos = 0;
        while(rpos < conn->len)
        {
            http_request_t req = {};
            size_t consumed = 0;

            http_parse_result_e ret = http_parse_request(conn->buffer, conn->len, &req, &consumed);
            if(ret == HTTP_PARSE_COMPLETE)              // 解析出完整请求
            {
                dbg("parse done complete");
                _web_process_http_req(fd, &req);

                // 清除req，继续解析
                memset(&req, 0, sizeof(req));
                rpos += consumed;
            }
            else if(ret == HTTP_PARSE_INCOMPLETE)       // 请求不完整，退出等待下个数据包
                break;
            else if(ret == HTTP_PARSE_ERROR)            // 内容出错
            {
                close(fd);      // 内含epoll_del
                web_connection_list_del(&g_web_conn_list, conn);
                mp_fixed_node_put(conn);        // 归还内存
                return;
            }
        }

        // 存在拆包的话，将剩余数据保存
        if(rpos > 0)
        {
            conn->len -= rpos;
            if(conn->len > 0)
                memmove(conn->buffer, conn->buffer + rpos, conn->len);
        }
    }
}

static inline void* _web_epoll_routine(void *args)
{
    struct epoll_event events[WEB_CONNECT_MAX_CNT];

    ATOM_STORE(&g_web_mgnter.running, 1, MORDER_RELAXED);

    dbg("web epoll routine starting");

    while(ATOM_LOAD(&g_web_mgnter.running, MORDER_ACQUIRE))
    {
        int ready = epoll_wait(g_web_mgnter.epoll_fd, events, WEB_CONNECT_MAX_CNT, WEB_EPOLL_WAIT_MS);
        if(ready == 0)          // 超时
            continue;
        else if(ready < 0)
            if(errno == EINTR)  // 被信号中断
                continue;
            else                // 出错
                break;
        else                    // 有事件就绪
            for(int i = 0; i < ready; ++ i)
                _web_events_handle(events[i].data.fd);
    }

    dbg_error("web epoll routine exit");

    return NULL;
}

static error_code_e _web_mgnt_init(void)
{
    // 创建socket
    g_web_mgnter.sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    pfm_ensure_done(g_web_mgnter.sock_fd >= 0);
    // 设置地址复用
    int opt = 1;
    setsockopt(g_web_mgnter.sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // socket绑定到any:8080
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr = INADDR_ANY,
        .sin_port = htons(WEB_TCP_PORT),
    };
    pfm_ensure_done(0 == bind(g_web_mgnter.sock_fd, (struct sockaddr*)&addr, sizeof(addr)));

    // 开始监听
    pfm_ensure_done(0 == listen(g_web_mgnter.sock_fd, WEB_CONNECT_MAX_CNT));

    // 创建epoll
    g_web_mgnter.epoll_fd = epoll_create1(O_CLOEXEC);
    pfm_ensure_done(g_web_mgnter.epoll_fd >= 0);
    // 加入epoll监听
    struct epoll_event ev = {.events = EPOLLIN | EPOLLET, .data.fd = g_web_mgnter.sock_fd};
    pfm_ensure_done(0 == epoll_ctl(g_web_mgnter.epoll_fd, EPOLL_CTL_ADD, g_web_mgnter.sock_fd, &ev));

    // 创建线程执行epoll_wait
    pthread_create(&g_web_mgnter.tid, NULL, _web_epoll_routine, NULL);

    return ERR_NO_ERROR;

done:
    if(g_web_mgnter.sock_fd >= 0)
        close(g_web_mgnter.sock_fd);
    if(g_web_mgnter.epoll_fd >= 0)
        close(g_web_mgnter.epoll_fd);
    return ERR_WEB_INIT_FAIL;
}

/**
 * @brief       cb for web pkt
 * 
 * @note        捕获dstport 8080 tcp报文回调，不做任何操作，仅为增加zcap计数
 */
static attr_force_inline void zcap_web_pkt_cb(char *packet, int len, void *args)
{
    // nothing
}

void web_module_init()
{
    mem_type_attr_register(web_connection);
    web_connection_list_init(&g_web_conn_list);

    _web_mgnt_init();

    // 注册zcap捕获http报文
    zcap_register_pkt_filter("wlan0", "web", zcap_web_pkt_cb, NULL);
    zcap_register_field_len_type("wlan0", "web", IPV4, 0xffff);
    zcap_register_field_pro_type("wlan0", "web", TCP, 0xff);
    zcap_register_field_l4_dport("wlan0", "web", WEB_TCP_PORT, 0xffff);
    zcap_register_pkt_filter("eth0", "web", zcap_web_pkt_cb, NULL);
    zcap_register_field_len_type("eth0", "web", IPV4, 0xffff);
    zcap_register_field_pro_type("eth0", "web", TCP, 0xff);
    zcap_register_field_l4_dport("eth0", "web", WEB_TCP_PORT, 0xffff);

    dbg_major("web module init ok");
}

void web_module_deinit()
{
    // 销毁web_connection list

    dbg_major("web module deinit ok");
}