/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    uss.h
 * @brief   ipc-unix_stream_socket实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-09-28
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-09-28 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/errno.h>
#include <unistd.h>
#include <stdlib.h>

#include "ipc/uss/uss.h"
#include "mp/mp.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// unix steam socket path长度
#define USS_SOCKET_PATH_SIZE        (108)

// listen上限
#define USS_SOCKET_LISTEN_CNT       (32)

// sender创建重试次数
#define USS_SENDER_RETRY_TIMES      (10)
// sender创建重试间隔
#define USS_SENDER_RETRY_INTERVAL   (100)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// uss管理结构定义
struct uss_mgnt_s{
    int fd;
    int is_receiver;        // 是否接收端
    char sock_path[USS_SOCKET_PATH_SIZE];
};

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       write exactly
 * 
 * @param[in]   fd      - fd
 * @param[in]   buf     - data buffer
 * @param[in]   len     - data len
 * 
 * @note        确保发送完全
 */
static attr_force_inline ssize_t _uss_write_exact(int fd, const void *buf, size_t len);

/**
 * @brief       read exactly
 * 
 * @param[in]   fd
 * @param[in]   buf     - buffer
 * @param[in]   len     - buf len
 * 
 * @note        确保读取完全
 */
static attr_force_inline ssize_t _uss_read_exact(int fd, void *buf, size_t len);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

uss_mgnt_t* uss_mgnter_create(const char *sock_path, int is_receiver)
{
    pfm_ensure_ret(sock_path, NULL);

    uss_mgnt_t *uss_mgnter = mp_calloc(1, sizeof(uss_mgnt_t));
    pfm_ensure_ret(uss_mgnter, NULL);
    strncpy(uss_mgnter->sock_path, sock_path, sizeof(uss_mgnter->sock_path)-1);
    uss_mgnter->is_receiver = is_receiver;

    uss_mgnter->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(unlikely(uss_mgnter->fd < 0))
    {
        dbg_error("socket() fail, error: %s", strerror(errno));
        goto done;
    }

    struct sockaddr_un addr = {
        .sun_family = AF_UNIX,
    };
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path)-1);
    if(is_receiver)
    {
        unlink(uss_mgnter->sock_path);
        if(bind(uss_mgnter->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        {
            dbg_always("bind() fail: %s", strerror(errno));
            goto done;
        }
        if(listen(uss_mgnter->fd, USS_SOCKET_LISTEN_CNT) < 0)
        {
            dbg_always("listen() fail: %s", strerror(errno));
            goto done;
        }
    }
    else
    {
        int retry = 0;
        do{
            if(connect(uss_mgnter->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
                usleep(USS_SENDER_RETRY_INTERVAL * 1000);
            else
                break;
        }while((retry++) < USS_SENDER_RETRY_TIMES);
    }

    return uss_mgnter;

done:
    if(uss_mgnter)
    {
        if(uss_mgnter->fd >= 0)     close(uss_mgnter->fd);
        mp_free(uss_mgnter, sizeof(uss_mgnt_t));
    }
    return NULL;
}

uss_mgnt_t* uss_mgnter_accept(uss_mgnt_t *uss_receiver)
{
    pfm_ensure_ret(uss_receiver, NULL);

    int client_fd = accept(uss_receiver->fd, NULL, NULL);
    if(client_fd < 0)
    {
        dbg_always("accept() fail: %s", strerror(errno));
        return NULL;
    }

    uss_mgnt_t *uss_client = mp_calloc(1, sizeof(uss_mgnt_t));
    if(unlikely(!uss_client))
    {
        dbg_always("no memory");
        close(client_fd);
        return NULL;
    }

    uss_client->fd = client_fd;
    uss_client->is_receiver = 0;

    return uss_client;
}

void uss_mgnter_destory(uss_mgnt_t *uss_mgnter)
{
    if(uss_mgnter)
    {
        if(uss_mgnter->fd >= 0)
            close(uss_mgnter->fd);
        if(uss_mgnter->is_receiver && uss_mgnter->sock_path[0] != '\0')
            unlink(uss_mgnter->sock_path);
        mp_free(uss_mgnter, sizeof(uss_mgnt_t));
    }
}

error_code_e uss_send(uss_mgnt_t *uss_mgnter, const void *data, size_t len)
{
    pfm_ensure_ret(uss_mgnter && data && len, ERR_BAD_PARAM);

    error_code_e ret;

    // 先发送长度
    uint32_t net_len = len;
    if(ERR_NO_ERROR != (ret = _uss_write_exact(uss_mgnter->fd, &net_len, sizeof(net_len))))
        return ret;
    // 再发送内容
    if(ERR_NO_ERROR != (ret = _uss_write_exact(uss_mgnter->fd, data, len)))
        return ret;

    return ERR_NO_ERROR;
}

ssize_t uss_recv(uss_mgnt_t *uss_mgnter, void *buf, size_t buf_size)
{
    pfm_ensure_ret(uss_mgnter && buf && buf_size, -1);

    // 先读取长度
    uint32_t msg_len = 0;
    ssize_t ret = _uss_read_exact(uss_mgnter->fd, &msg_len, sizeof(msg_len));
    if(ret <= 0)
        return -1;
    // 检查缓冲区是否足够
    if(msg_len > buf_size)
        return -1;

    // 读取数据
    if(msg_len > 0)
    {
        ret = _uss_read_exact(uss_mgnter->fd, buf, msg_len);
        if(ret < 0)
            return -1;
    }

    return (ssize_t)msg_len;
}

static inline ssize_t _uss_write_exact(int fd, const void *buf, size_t len)
{
    size_t total = 0;
    while(total < len)
    {
        ssize_t n = write(fd, (const char*)buf + total, len - total);
        if(n > 0)
            total += n;
        else if(n < 0)
        {
            if(errno == EINTR)
                continue;
            return -1;
        }
    }
    return (ssize_t)total;
}

static inline ssize_t _uss_read_exact(int fd, void *buf, size_t len)
{
    size_t total = 0;
    while(total < len)
    {
        ssize_t n = read(fd, (char*)buf + total, len - total);
        if(n > 0)
            total += n;
        else if(n == 0)     // 对端关闭
            return 0;
        else
        {
            if(errno == EINTR)
                continue;
            return -1;
        }
    }
    return (ssize_t)total;
}