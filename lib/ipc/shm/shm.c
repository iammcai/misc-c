/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    shm.h
 * @brief   ipc-share memory 共享内存实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-10-8
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-10-8 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/errno.h>
#include <unistd.h>
#include <sys/stat.h>

#include "ipc/shm/shm.h"
#include "mp/mp.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define SHM_NAME_SIZE       (128)   // name长度

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 定义共享内存头部类型
struct shm_mgnt_s{
    int fd;     // shm open fd
    char name[SHM_NAME_SIZE];           // name
    void *map_ptr;          // 映射后的内存指针
    size_t size;            // shm大小
    int is_creater;         // 创建者标志
    pthread_mutex_t *lock;  // 指向互斥锁
};

// 共享内存布局定义
typedef struct{
    pthread_mutex_t lock;       // mutex
    char data[];                // 数据内容
}shm_layout_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

shm_mgnt_t* shm_mgnt_open(const char *name, size_t size, int is_creater)
{
    pfm_ensure_ret(name && name[0] == '/' && size > 0, NULL);

    shm_mgnt_t *shm = mp_calloc(1, sizeof(shm_mgnt_t));
    pfm_ensure_ret(shm, NULL);

    strncpy(shm->name, name, sizeof(shm->name) - 1);
    shm->size = size;
    shm->is_creater = is_creater ? 1 : 0;

    int o_flag = O_RDWR;
    if(is_creater)
        o_flag |= O_CREAT;
    shm->fd = shm_open(shm->name, o_flag, 0666);
    if(shm->fd < 0)
    {
        dbg_error("shm_open() fail: %s", strerror(errno));
        goto error;
    }
    // 设置大小
    size_t total_size = sizeof(shm_layout_t) + size;
    if(is_creater && ftruncate(shm->fd, total_size) < 0)
    {
        dbg_error("ftruncate() fail: %s", strerror(errno));
        goto error;
    }
    // 映射到当前进程地址
    shm->map_ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm->fd, 0);
    if(shm->map_ptr == MAP_FAILED)
    {
        dbg_error("mmap() fail: %s", strerror(errno));
        goto error;
    }
    shm_layout_t *layout = (shm_layout_t*)shm->map_ptr;
    shm->lock = &layout->lock;

    // 初始化锁
    if(is_creater)
    {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        // 设置进程间共享
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        // 设置健壮，防止进程崩溃导致死锁
        pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
        if(0 != pthread_mutex_init(shm->lock, &attr))
        {
            dbg_error("mutex attr set fail");
            pthread_mutexattr_destroy(&attr);
            goto error;
        }
        pthread_mutexattr_destroy(&attr);
    }

    // 可关闭fd
    close(shm->fd);
    shm->fd = -1;

    return shm;

error:
    if(shm)
    {
        if(MAP_FAILED != shm->map_ptr)
            munmap(shm->map_ptr, total_size);
        mp_free(shm, sizeof(shm_mgnt_t));
    }
    return NULL;
}

error_code_e shm_mgnt_close(shm_mgnt_t *shm)
{
    pfm_ensure_ret(shm, ERR_BAD_PARAM);

    if(shm->map_ptr && shm->map_ptr != MAP_FAILED)
    {
        if(shm->is_creater && shm->lock)
            pthread_mutex_destroy(shm->lock);
        munmap(shm->map_ptr, shm->size + sizeof(shm_layout_t));
    }
    // 关闭fd
    if(shm->fd >= 0)
        close(shm->fd);
    // 删除/*文件
    if(shm->is_creater)
        shm_unlink(shm->name);

    mp_free(shm, sizeof(shm_mgnt_t));

    return ERR_NO_ERROR;
}

error_code_e shm_mgnt_write(shm_mgnt_t *shm, size_t offset, const void *data, size_t len)
{
    pfm_ensure_ret(shm && data && (offset + len) <= shm->size, ERR_BAD_PARAM);

    // lock
    int ret = pthread_mutex_lock(shm->lock);
    if(ret == EOWNERDEAD)
        pthread_mutex_consistent(shm->lock);
    else if(ret != 0)
        return ERR_SYS_API_ERROR;

    shm_layout_t *layout = (shm_layout_t*)shm->map_ptr;
    char *dest = (char*)layout->data + offset;
    memcpy(dest, data, len);

    // unlock
    pthread_mutex_unlock(shm->lock);

    return ERR_NO_ERROR;
}

error_code_e shm_mgnt_read(shm_mgnt_t *shm, size_t offset, void *buf, size_t len)
{
    pfm_ensure_ret(shm && buf && (offset + len <= shm->size), ERR_BAD_PARAM);

    // lock
    int ret = pthread_mutex_lock(shm->lock);
    if(ret == EOWNERDEAD)
        pthread_mutex_consistent(shm->lock);
    else if(ret != 0)
        return ERR_SYS_API_ERROR;

    shm_layout_t *layout = (shm_layout_t*)shm->map_ptr;
    char *src = (char*)layout->data + offset;
    memcpy(buf, src, len);

    // unlock
    pthread_mutex_unlock(shm->lock);

    return ERR_NO_ERROR;
}