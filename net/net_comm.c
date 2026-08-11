/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    net_comm.c
 * @brief   网络公用接口实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-08-11
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-08-11 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <errno.h>

#include "net_comm.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 查询接口状态的消息定义
typedef struct{
    struct nlmsghdr nh;
    struct ifinfomsg ifi;
} attr_packed nl_req_t;

// 查询v4地址的消息定义
typedef struct{
    struct nlmsghdr nh;
    struct ifaddrmsg ifa;
} attr_packed nl_req_addr_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

error_code_e if_status_get(const char *if_name, if_status_t *status)
{
    pfm_ensure_ret(if_name && status, ERR_BAD_PARAM);

    // 获取if index
    unsigned int if_index = if_nametoindex(if_name);
    pfm_ensure_ret(if_index > 0, ERR_NT_IF_ERROR);

    /* =-=-=-=-=-=-=-=- 查询linkup -=-=-=-=-=-=-=-= */

    // 创建socket用于netlink
    int sock_fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
    pfm_ensure_ret(sock_fd > 0, ERR_NT_SOCK_ERROR);
    // 发送消息
    nl_req_t req = {};
    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));      // NLMSG_LENGTH 宏包含了hdr长度
    req.nh.nlmsg_type = RTM_GETLINK;
    req.nh.nlmsg_flags = NLM_F_REQUEST;
    req.ifi.ifi_index = if_index;
    if(send(sock_fd, &req, req.nh.nlmsg_len, 0) < 0)
    {
        close(sock_fd);
        return ERR_NT_SOCK_ERROR;
    }
    // 读取netlink消息
    char buf[4096] = {};
    ssize_t len = recv(sock_fd, buf, sizeof(buf), 0);
    if(len <= 0)
    {
        close(sock_fd);
        return ERR_NT_SOCK_ERROR;
    }
    nl_req_t *nl_resp = (nl_req_t*)buf;
    if(nl_resp->nh.nlmsg_type == NLMSG_ERROR || nl_resp->nh.nlmsg_type != RTM_NEWLINK)
    {
        close(sock_fd);
        return ERR_NT_SOCK_ERROR;
    }
    status->linkup = (nl_resp->ifi.ifi_flags & IFF_RUNNING) ? 1 : 0;    // 检查linkup标志

    /* =-=-=-=-=-=-=-=- 获取ipv4地址 -=-=-=-=-=-=-=-= */

    nl_req_addr_t req_addr = {};
    req_addr.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    req_addr.nh.nlmsg_type = RTM_GETADDR;
    req_addr.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req_addr.ifa.ifa_family = AF_INET;
    req_addr.ifa.ifa_index = if_index;
    // 发送给内核
    if(send(sock_fd, &req_addr, req_addr.nh.nlmsg_len, 0) < 0)
    {
        close(sock_fd);
        return ERR_NT_SOCK_ERROR;
    }
    status->has_ipv4 = 0;
    // 接收消息，可能有多个ipaddr，NLM_F_DUMP会单独发送
    while(1)
    {
        len = recv(sock_fd, buf, sizeof(buf), 0);
        if(len <= 0)        // 出错
            break;
        for(struct nlmsghdr *nlh = (struct nlmsghdr*)buf; NLMSG_OK(nlh, (unsigned int)len); nlh = NLMSG_NEXT(nlh, len))
        {
            if(nlh->nlmsg_type == NLMSG_DONE)
                goto done;
            if(nlh->nlmsg_type != RTM_NEWADDR)
                continue;
        
            struct ifaddrmsg *ifa = (struct ifaddrmsg*)NLMSG_DATA(nlh);
            if(ifa->ifa_index == if_index && ifa->ifa_family == AF_INET)
            {
                status->has_ipv4 = 1;
                goto done;
            }
        }
    }

done:
    close(sock_fd);
    return ERR_NO_ERROR;
}