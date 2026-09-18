/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    uds.c
 * @brief   ipc-unix_dgram_socket实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-09-18
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-09-18 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <sys/errno.h>
#include <unistd.h>         // for unlink

#include "ipc/uds/uds.h"
#include "mp/mp.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define UDS_SOCK_PATH_SIZE      (108)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// uds管理结构定义
struct uds_mgnt_s
{
    int fd;                 // sock fd
    int is_receiver;        // 是否接收端
    char sock_path[108];    // sock path
    struct sockaddr_un dest_addr;       // 发送端目的地址
};

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       set fd blocking attr
 * 
 * @param[in]   fd
 * @param[in]   blocking    - block or not
 * 
 * @retval      error code
 */
static attr_force_inline int _fd_blocking(int fd, int blocking)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if(-1 == flags) return -1;
    if(!blocking)
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    else
        return fcntl(fd, F_SETFL, flags & (~O_NONBLOCK));
}

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

uds_mgnt_t* uds_mgnt_create(const char *sock_path, int is_recv)
{
    pfm_ensure_ret(sock_path && strlen(sock_path) < UDS_SOCK_PATH_SIZE, NULL);

    uds_mgnt_t *uds_mgnt = mp_calloc(1, sizeof(uds_mgnt_t));
    pfm_ensure_ret(uds_mgnt, NULL);

    uds_mgnt->is_receiver = is_recv ? 1 : 0;
    strncpy(uds_mgnt->sock_path, sock_path, sizeof(uds_mgnt->sock_path) - 1);

    // 创建socket 指定unix dgram
    uds_mgnt->fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if(uds_mgnt->fd < 0)
    {
        dbg_error("socket() fail, err: %s", strerror(errno));
        goto error;
    }

    if(uds_mgnt->is_receiver)       // 接收端bind
    {
        struct sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, uds_mgnt->sock_path, sizeof(addr.sun_path) - 1);
        unlink(uds_mgnt->sock_path);        // 清理旧文件，重要
        if(bind(uds_mgnt->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        {
            dbg_error("receiver bind() fail, err: %s", strerror(errno));
            goto error;
        }
        // 保持默认block，需要时再设置nonblock

        dbg_always("uds receiver create ok, path %s", uds_mgnt->sock_path);
    }
    else        // 发送端connect
    {
        memset(&uds_mgnt->dest_addr, 0, sizeof(uds_mgnt->dest_addr));
        uds_mgnt->dest_addr.sun_family = AF_UNIX;
        strncpy(uds_mgnt->dest_addr.sun_path, uds_mgnt->sock_path, sizeof(uds_mgnt->dest_addr.sun_path) - 1);
        if(connect(uds_mgnt->fd, (struct sockaddr*)&uds_mgnt->dest_addr, sizeof(uds_mgnt->dest_addr)) < 0)
        {
            dbg_error("sender connect() fail, err: %s", strerror(errno));
            goto error;
        }
        // 设置nonblock，防止发送端卡死
        _fd_blocking(uds_mgnt->fd, 0);

        dbg_always("uds sender create ok, path %s", uds_mgnt->sock_path);
    }

    return uds_mgnt;

error:
    if(uds_mgnt)
    {
        if(uds_mgnt->fd)    close(uds_mgnt->fd);
        mp_free(uds_mgnt, sizeof(uds_mgnt_t));
    }
    return NULL;
}

void uds_mgnt_destroy(uds_mgnt_t *uds_mgnt)
{
    if(uds_mgnt)
    {
        if(uds_mgnt->fd)    close(uds_mgnt->fd);
        if(uds_mgnt->is_receiver)   unlink(uds_mgnt->sock_path);

        mp_free(uds_mgnt, sizeof(*uds_mgnt));
    }
}

error_code_e uds_send(uds_mgnt_t *uds_mgnt, const void *data, size_t len)
{
    pfm_ensure_ret(uds_mgnt && data && len > 0, ERR_BAD_PARAM);

    error_code_e ret = ERR_NO_ERROR;
    ssize_t send_bytes = send(uds_mgnt->fd, data, len, MSG_DONTWAIT);
    if(send_bytes < 0)
    {
        dbg_error("send fail, error: %s", strerror(errno));
        ret = ERR_SYS_API_ERROR;
    }

    return ret;
}

ssize_t uds_recv(uds_mgnt_t *uds_mgnt, void *buf, size_t buf_size)
{
    pfm_ensure_ret(uds_mgnt && buf && buf_size > 0, ERR_BAD_PARAM);

    ssize_t recv_bytes = recv(uds_mgnt->fd, buf, buf_size, 0);

    if(recv_bytes >= 0)
        return recv_bytes;
    else
    {
        if(errno == EINTR)
        {
            dbg("interrupted by signal, please retry");
            return 0;
        }
        else
        {
            dbg_error("recv fail, error: %s", strerror(errno));
            return -1;
        }
    }
}

ssize_t uds_try_recv(uds_mgnt_t *uds_mgnt, void *buf, size_t buf_size)
{
    pfm_ensure_ret(uds_mgnt && buf && buf_size > 0, ERR_BAD_PARAM);

    ssize_t recv_bytes = recv(uds_mgnt->fd, buf, buf_size, MSG_DONTWAIT);       // 传入MSG_DONTWAIT表明此处按照非阻塞，不改变fd属性

    if(recv_bytes >= 0)
        return recv_bytes;
    else
    {
        if(errno == EINTR)
        {
            dbg("interrupted by signal, please retry");
            return 0;
        }
        else
        {
            dbg_error("recv fail, error: %s", strerror(errno));
            return -1;
        }
    }
}