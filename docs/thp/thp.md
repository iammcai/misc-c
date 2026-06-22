# thread pool

本目录实现一个线程池。所谓线程池，和内存池类似，先创建多个线程进入等待状态，当工作来临时，唤醒其中一个线程执行工作，然后继续等待新的任务来临。目的是减少创建/销毁线程的开销

## 设计思路

线程池的定义如下

```C
// 线程池管理结构定义
typedef struct{
    const char *name;           // 线程池名字

    unsigned char thread_count; // 工作线程数量
    pthread_t *thread_array;    // 工作线程

    pthread_mutex_t mtx;        // 互斥锁
    pthread_cond_t cond;        // 条件变量
    thp_work_list_head_t wl;    // 工作队列

    unsigned char shutdown;     // 线程池关闭标志

    thp_hash_item_t item;       // 哈希表Item
}thp_t;

// 工作任务定义
typedef struct{
    thp_work_func wf;           // 回调函数
    void *args;                 // 参数
    thp_work_list_item_t item;  // list item
}thp_work_t;
```