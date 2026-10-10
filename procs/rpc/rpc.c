/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    rpc.h
 * @brief   RPC实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-09-29
 * @version 1.0
 *
 * @note    整体为星型架构，一个RPC进程负责路由转发
 *          rpc broker  --- core proc
 *                      --- test proc
 *                      --- ...
 *          例如core调用test进程结构，需要先发给rpc broker处理，再rpc broker发给test进程
 *
 *          IPC使用unix stream socket，rpc作为receiver
 * 
 *          rpc通过epoll管理socket，使用ET+nonblocking
 *
 * @history
 *   1.0 | 2026-09-29 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/errno.h>
#include <sys/prctl.h>
#include <sys/select.h>

#include "rpc/rpc.h"
#include "ipc/uss/uss.h"
#include "plat/atom.h"
#include "ipc/shm/shm.h"
#include "fifo/fifo.h"
#include "mp/mp.h"
#include "type/type_hash.h"
#include "event/ev_lock.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// epoll监听最大数量
#define RPC_EPOLL_LISTEN_CNT    (PID_CNT * 2)

// epoll等待时间
#define RPC_EPOLL_WAIT      (2000)

// rpc func name长度
#define RPC_FUNC_NAME_SIZE  (32)

// 单次读取rpc buffer长度
#define PRC_READ_ONCE_SIZE  (2 << 10)
// buffer长度，必须2次幂
#define RPC_FIFO_SIZE               (PRC_READ_ONCE_SIZE << 3)
// 掩码
#define RPC_FIFO_MASK               (RPC_FIFO_SIZE - 1)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// rpc进程uss管理
typedef struct{
    uss_mgnt_t *listener;
    uss_mgnt_t *accepter;       // accept后返回的新的uss
}rpc_uss_mgnt_t;

// rpc用户输入参数定义
struct rpc_in_param_s{
    int todo;
};

// rpc头部结构定义
struct rpc_header_s{
    proc_id_e src;      // 调用方pid
    proc_id_e dest;     // 目的pid
    char func[RPC_FUNC_NAME_SIZE];          // 函数名
    int sync;           // 是否同步call
    size_t size;        // ctx长度
};

// 预定义hash结构，用于存储注册rpc服务函数
pre_declare_hash(rpc_server_func_entry)

// rpc注册函数结构定义
typedef struct{
    char name[RPC_FUNC_NAME_SIZE];
    rpc_server_func server_func;                // 服务函数
    rpc_server_func_entry_hash_item_t item;     // hash item
}rpc_server_func_entry_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       rpc proc epoll thread routine
 */
static void* _rpc_epoll_thd_routine(void *args);

/**
 * @brief       check if ready fd is new connection
 * 
 * @param[in]   fd
 * @param[out]  pid_out     - connect pid
 * 
 * @retval      0 - not new conn, 1 - y
 */
static attr_force_inline int _rpc_check_new_conn(int fd, proc_id_e *pid_out);

/**
 * @brief       handle new connection
 * 
 * @param[in]   pid     - pid
 * 
 * @note        rpc进程调用。accept新连接，创建accepter，加入到epoll监听
 */
static void _rpc_handle_new_conn(proc_id_e pid);

/**
 * @brief       handle msg
 * 
 * @param[in]   fd
 * 
 * @note        处理新消息，读取流式信息到fifo中待处理，避免粘包半包
 */
static void _rpc_handle_msg(int fd);

/**
 * @brief       handle rpc fifo msg
 * 
 * @note        处理fifo中缓存的rpc消息
 */
static void _rpc_fifo_handle(void);

/**
 * @brief       init rpc module for rpc process
 * 
 * @note        创建uss通道，作为receiver，需要处理accept
 */
static void _rpc_module_init_for_rpc(void);

/**
 * @brief       init rpc module for other process
 * 
 * @note        初始化uss通道，作为sender connect
 */
static void _rpc_module_init_for_others(void);

/**
 * @brief       exit rpc module for rpc process
 */
static void _rpc_module_exit_for_rpc(void);

/**
 * @brief       exit rpc module for non rpc proc
 */
static void _rpc_module_exit_for_others(void);

/**
 * @brief       non rpc proc select thread
 * 
 * @note        非rpc进程使用select监听uss，单独线程执行
 */
static void* _rpc_select_routine(void *args);

/**
 * @brief       cmp func of rpc_server_func_entry_t
 */
static attr_pure_inline int _rpc_server_func_entry_cmp(rpc_server_func_entry_t *e1, rpc_server_func_entry_t *e2)
{
    pfm_ensure_ret(e1 && e1->name && e2 && e2->name, -1);
    return strcmp(e1->name, e2->name);
}

/**
 * @brief       hash func of rpc_server_func_entry_t
 */
static attr_pure_inline unsigned int _rpc_server_func_entry_hash(rpc_server_func_entry_t *e)
{
    pfm_ensure_ret(e && e->name, 0);
    return type_hash_jhash(e->name, strlen(e->name), 0);
}

// hash函数定义
declare_hash(rpc_server_func_entry, rpc_server_func_entry, rpc_server_func_entry_t, item, 1, 31, _rpc_server_func_entry_cmp, _rpc_server_func_entry_hash);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 全局socket path注册表
static const char* g_rpc_uss_path_table[PID_CNT] = {
    [PID_CORE] = "/tmp/core.sock",
    [PID_TEST] = "/tmp/test.sock",
};

// rpc进程使用，与其它进程的ipc通道receiver, accepter数组
static rpc_uss_mgnt_t g_rpc_uss_mgnt_table[PID_CNT] = {};

// 非rpc进程使用，与rpc之间的ipc通道sender
static uss_mgnt_t *g_rpc_uss_mgnt = NULL;

// rpc进程epoll线程id
static pthread_t g_rpc_epoll_tid;
// epoll fd
static int g_rpc_epoll_fd;
// 运行标志
static ATOMIC_UINT8_T g_rpc_epoll_thd_running = 0;

// 非rpc进程使用，select监听uss线程id
static pthread_t g_rpc_select_tid;
// 运行标志
static ATOMIC_UINT8_T g_rpc_select_thd_running = 0;

// rpc process运行标志
static shm_mgnt_t *rpc_process_running = NULL;

// rpc ring buffer
static fifo_t* g_rpc_fifo = NULL;

// 所有进程的RPC服务函数注册表，进程独立，动态注册
static rpc_server_func_entry_hash_head_t g_rpc_server_func_entry_table = {};
// lock for g_rpc_server_func_entry_table
static ev_rwlock_t g_rpc_server_func_entry_table_rwlock = EV_RWLOCK_INITIALIZER;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void rpc_server_func_register(const char *name, rpc_server_func func)
{
    pfm_ensure_ret_void(name && func);

    rpc_server_func_entry_t *entry = mp_calloc(1, sizeof(rpc_server_func_entry_t));
    pfm_ensure_ret_void(entry);
    entry->server_func = func;
    strncpy(entry->name, name, RPC_FUNC_NAME_SIZE - 1);

    ev_with_wrlock(&g_rpc_server_func_entry_table_rwlock)
        rpc_server_func_entry_hash_add(&g_rpc_server_func_entry_table, entry);
}

static inline int _rpc_check_new_conn(int fd, proc_id_e *pid_out)
{
    pfm_ensure_ret(pid_out, 0);
    for(proc_id_e pid = PID_CORE; pid < PID_CNT; ++ pid)
        if(fd == uss_mgnter_fd_get(g_rpc_uss_mgnt_table[pid].listener))
        {
            *pid_out = pid;
            return 1;
        }
    return 0;
}

static void _rpc_handle_new_conn(proc_id_e pid)
{
    pfm_ensure_ret_void(pid >= PID_CORE && pid < PID_CNT);

    // 每个pid只有一个连接，所以不while(1) accept
    g_rpc_uss_mgnt_table[pid].accepter = uss_mgnter_accept(g_rpc_uss_mgnt_table[pid].listener, 1);
    pfm_ensure_ret_void(g_rpc_uss_mgnt_table[pid].accepter);

    int fd = uss_mgnter_fd_get(g_rpc_uss_mgnt_table[pid].accepter);
    struct epoll_event ev = {
        .data.fd = fd,
        .events = EPOLLIN | EPOLLET,
    };
    pfm_ensure_ret_void(0 == epoll_ctl(g_rpc_epoll_fd, EPOLL_CTL_ADD, fd, &ev));

    dbg_major("handle new rpc connect for pid %d ok", pid);
}

static void _rpc_handle_msg(int fd)
{
    pfm_ensure_ret_void(fd >= 0);

    char buf[PRC_READ_ONCE_SIZE] = {};

    // IO需要读取完，不残留
    while(1)
    {
        // 根据fd找到对应的receiver
        uss_mgnt_t *receiver = NULL;
        for(proc_id_e pid = PID_CORE; pid < PID_CNT; ++ pid)
            if(uss_mgnter_fd_get(g_rpc_uss_mgnt_table[pid].accepter) == fd)
            {
                receiver = g_rpc_uss_mgnt_table[pid].accepter;
                break;
            }
        pfm_ensure_ret_void(receiver);

        // 读取到临时缓存
        memset(buf, 0, PRC_READ_ONCE_SIZE);
        ssize_t read_size = uss_recv(receiver, buf, PRC_READ_ONCE_SIZE);
        if(0 == read_size)
            break;
        else if(read_size < 0)
        {
            if(EINTR == errno)
                continue;
            dbg_error("uss_recv fail: %s", strerror(errno));
            break;
        }

        // 复制到全局环形缓冲
        size_t remain = read_size;
        while(remain)
        {
            size_t write_size = fifo_write(g_rpc_fifo, buf, remain);
            _rpc_fifo_handle();
            remain -= write_size;
        }

        // read长度小于一次最大值，说明已经读取完
        if(read_size < PRC_READ_ONCE_SIZE)
            break;
    }
}

static void _rpc_fifo_handle(void)
{
    while(1)
    {
        size_t data_size = fifo_get_used(g_rpc_fifo);       // 获取当前fifo数据长度
        if(data_size < sizeof(rpc_header_t))                // 不足一个rpc header，直接返回
            break;

        rpc_header_t header = {};
        fifo_read(g_rpc_fifo, (char*)&header, sizeof(rpc_header_t), 0);
        if(data_size < (sizeof(rpc_header_t) + header.size))            // 不足一个rpc header + ctx，直接返回
            break;

        // 正式处理
        size_t total_size = sizeof(rpc_header_t) + header.size;
        char *msg = mp_calloc(1, total_size);                       // TODO: pool

        fifo_read(g_rpc_fifo, msg, total_size, 1);
        rpc_header_t *p_header = (rpc_header_t*)msg;
        dbg_always("src_pid %d, dest_pid %d, func_name %s, sync %d, size %d", 
            p_header->src, p_header->dest, p_header->func, p_header->sync, p_header->size);

        if((PROC_ID_IS_VALID(p_header->src) && PROC_ID_IS_VALID(p_header->dest)))
        {
            // 根据fd找到对应的receiver
            uss_mgnt_t *sender = g_rpc_uss_mgnt_table[p_header->dest].accepter;
            if(ERR_NO_ERROR != uss_send(sender, p_header, total_size))
                dbg_error("uss_send fail()");
        }
        else
            dbg_error("error rpc msg");

        mp_free(msg, total_size);
    }
}

static void _rpc_module_init_for_rpc(void)
{
    for(proc_id_e pid = PID_CORE; pid < PID_CNT; ++ pid)
    {
        if(pid == PID_RPC)
            continue;
        g_rpc_uss_mgnt_table[pid].listener = uss_mgnter_create(g_rpc_uss_path_table[pid], 1, 1);
        if(!g_rpc_uss_mgnt_table[pid].listener)
            dbg_error("uss receiver create listener for proc id %d fail", pid);
    }
}

static void _rpc_module_init_for_others(void)
{
    proc_id_e pid = proc_id_get();

    g_rpc_uss_mgnt = uss_mgnter_create(g_rpc_uss_path_table[pid], 0, 1);
    if(unlikely(!g_rpc_uss_mgnt))
    {
        dbg_error("uss sender create for proc id %d fail", pid);
        return;
    }

    rpc_server_func_entry_hash_init(&g_rpc_server_func_entry_table);    // init hashtable

    pthread_create(&g_rpc_select_tid, NULL, _rpc_select_routine, NULL);
}

static void* _rpc_select_routine(void *args)
{
    ATOM_STORE(&g_rpc_select_thd_running, 1, MORDER_RELAXED);

    fd_set readfds;
    struct timeval tv = {};
    int fd = uss_mgnter_fd_get(g_rpc_uss_mgnt);

    while(ATOM_LOAD(&g_rpc_select_thd_running, MORDER_RELAXED))
    {
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(uss_mgnter_fd_get(g_rpc_uss_mgnt)+1, &readfds, NULL, NULL, &tv);      // 阻塞等待就绪

        if(ret > 0)
        {
            if(FD_ISSET(fd, &readfds))
            {
                // 先读header
                rpc_header_t hdr = {};
                uss_recv(g_rpc_uss_mgnt, &hdr, sizeof(rpc_header_t));
                // TODO: 再读ctx

                dbg_always("recv rpc msg, src_pid %d, dest_pid %d, func %s, sync %d, size %d",
                    hdr.src, hdr.dest, hdr.func, hdr.sync, hdr.size);

                // TODO: 进程执行函数
                rpc_server_func_entry_t key = {}, *entry = NULL;
                strncpy(key.name, hdr.func, RPC_FUNC_NAME_SIZE - 1);
                ev_with_rdlock(&g_rpc_server_func_entry_table_rwlock)
                    entry = rpc_server_func_entry_hash_find(&g_rpc_server_func_entry_table, &key);
                if(entry)
                    entry->server_func(&hdr, NULL);         // 输入参数
                else
                    dbg_error("can't find server func for: %s", hdr.func);
            }
        }
        else if(ret == 0)       // timeout
        {}
        else
        {
            if(errno == EINTR)
                continue;
            dbg_error("select() fail: %s", strerror(errno));
            break;
        }
    }
}

void rpc_module_init()
{
    if(proc_id_get() == PID_RPC)
        _rpc_module_init_for_rpc();
    else
        _rpc_module_init_for_others();
}

static void _rpc_module_exit_for_rpc(void)
{
    proc_id_e pid;

    PROC_ID_ITER_ALL(pid)
    {
        if(pid == PID_RPC)
            continue;
        uss_mgnter_destory(g_rpc_uss_mgnt_table[pid].accepter);
        uss_mgnter_destory(g_rpc_uss_mgnt_table[pid].listener);
    }
}

static void _rpc_module_exit_for_others(void)
{
    // 结束select线程
    ATOM_STORE(&g_rpc_select_thd_running, 0, MORDER_RELAXED);
    pthread_join(g_rpc_select_tid, NULL);

    // 释放uss
    uss_mgnter_destory(g_rpc_uss_mgnt);

    // 清理rpc服务注册表
    ev_with_wrlock(&g_rpc_server_func_entry_table_rwlock)
    {
        rpc_server_func_entry_t *entry = NULL;
        while(entry = rpc_server_func_entry_hash_first(&g_rpc_server_func_entry_table))
        {
            rpc_server_func_entry_hash_del(&g_rpc_server_func_entry_table, entry);
            mp_free(entry, sizeof(rpc_server_func_entry_t));
        }
    }
}

void rpc_module_exit()
{
    if(proc_id_get() == PID_RPC)
        _rpc_module_exit_for_rpc();
    else
        _rpc_module_exit_for_others();
}

static void* _rpc_epoll_thd_routine(void *args)
{
    // epoll init
    g_rpc_epoll_fd = epoll_create1(O_CLOEXEC);
    pfm_ensure_ret(g_rpc_epoll_fd >= 0, NULL);
    // add epoll
    for(proc_id_e pid = PID_CORE; pid < PID_CNT; ++ pid)
    {
        if(pid == PID_RPC)
            continue;

        int fd = uss_mgnter_fd_get(g_rpc_uss_mgnt_table[pid].listener);
        struct epoll_event event = {
            .data.fd = fd,
            .events = EPOLLIN | EPOLLET,
        };
        pfm_ensure_ret(0 == epoll_ctl(g_rpc_epoll_fd, EPOLL_CTL_ADD, fd, &event), NULL);
    }

    prctl(PR_SET_NAME, "rpc_epoll");

    struct epoll_event events[RPC_EPOLL_LISTEN_CNT];
    ATOM_STORE(&g_rpc_epoll_thd_running, 1, MORDER_RELAXED);
    while(ATOM_LOAD(&g_rpc_epoll_thd_running, MORDER_RELAXED))
    {
        int nfds = epoll_wait(g_rpc_epoll_fd, events, RPC_EPOLL_LISTEN_CNT, RPC_EPOLL_WAIT);
        if(nfds < 0)
            if(errno == EINTR)      continue;
            else
            {
                dbg_error("epoll wait error: %s", strerror(errno));
                break;
            }

        for(int i = 0; i < nfds; ++ i)
        {
            int fd = events[i].data.fd;
            proc_id_e conn_pid;
            if(_rpc_check_new_conn(fd, &conn_pid))         // 处理新的连接
                _rpc_handle_new_conn(conn_pid);
            else            // 收到消息
                _rpc_handle_msg(fd);
        }
    }

    // exit
    close(g_rpc_epoll_fd);      // 自动del
}

void rpc_proc_entry()
{
    // 开辟共享内存用于检查运行标志
    rpc_process_running = shm_mgnt_open(RPC_PROCESS_RUNNING_SHM_NAME, sizeof(int), 1);
    pfm_ensure_ret_void(rpc_process_running);
    int run = 1;
    shm_mgnt_write(rpc_process_running, 0, &run, sizeof(run));

    // 创建环形缓冲用于分析uss rpc消息
    g_rpc_fifo = fifo_create(RPC_FIFO_SIZE);

    // epoll监听线程启动
    pthread_create(&g_rpc_epoll_tid, NULL, _rpc_epoll_thd_routine, NULL);

    while(1)
    {
        // 检查shm运行标志
        shm_mgnt_read(rpc_process_running, 0, &run, sizeof(run));
        if(!run)
            break;

        usleep(100 * 1000);
    }

    shm_mgnt_close(rpc_process_running);

    fifo_destroy(&g_rpc_fifo);

    ATOM_STORE(&g_rpc_epoll_thd_running, 0, MORDER_RELAXED);
    pthread_join(g_rpc_epoll_tid, NULL);

    // unlink all socket file
    for(proc_id_e pid = PID_CORE; pid < PID_CNT; ++ pid)
        unlink(g_rpc_uss_path_table[pid]);

    dbg_always("@@@ rpc process exit...");
}

// TODO: 
error_code_e rpc_call_func_async(proc_id_e dest, const char* func, rpc_in_param_t *in_param)
{
    pfm_ensure_ret(dest >= PID_CORE && dest < PID_CNT && func && strlen(func) < RPC_FUNC_NAME_SIZE, ERR_BAD_PARAM);

    // buffer初始化
    rpc_header_t buffer = {
        .src = proc_id_get(),
        .dest = dest,
        .sync = 0,              // 异步调用
    };
    strncpy(buffer.func, func, sizeof(buffer.func) - 1);

    size_t ctx_len = 0;

    // TODO: + ctx

    buffer.size = ctx_len;
    error_code_e ret = uss_send(g_rpc_uss_mgnt, &buffer, sizeof(rpc_header_t) + ctx_len);
    pfm_ensure_ret(ret == ERR_NO_ERROR, ret);

    dbg("rpc call func async done, src %d, dest %d, func %s", proc_id_get(), dest, func);

    return ERR_NO_ERROR;
}