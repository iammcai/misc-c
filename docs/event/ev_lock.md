# event lock

这里实现一个用户态锁的框架，旨在提供比pthread_mutex更高性能的同时，外部模块可以方便地使用锁

## 设计思路

### 读写锁

读写锁适合读场景远多于写场景的场合，比如缓存内存，经常需要读取，而只有缓存更新或者老化时才需要写

读写锁的设计思路是：没有写者时，每个读者都可以直接拿到读锁，有写者时则需要等待写者释放锁；多个写者中只有一个写者可以拿到锁，且需要没有读者正在读

为此，读写锁的定义如下，依旧使用原子操作来避免使用pthread_mutex，减小开销

```C
typedef struct{
    ATOMIC_UINT32_T readers;    // 读者数量
    ATOMIC_UINT8_T writing;     // 写者持有
}ev_rwlock_t;
```

加锁解锁的步骤如下：

- 获取读锁：轮询`writing`直到`writing==0`，然后`readers++`
- 释放读锁：`readers--`
- 获取写锁：先设置`writing=1`（使用CAS保证只有一个写者成功），避免新的读者进入，然后轮询`readers`直到`readers==0`
- 释放写锁：设置`writing=0`

以上的设计可以避免写者饥饿（写者等待拿写锁的同时，新的读者不断进入，导致写者拿锁失败）

实现中为了确保用户不会忘记解锁，实现了自动加解锁的宏。以读锁为例：

```C
/**
 * 外部使用，后接{}，代码块内的处理由读锁保护，异常退出或者正常退出均可安全释放
 */
#define ev_with_rdlock(lock)    \
    for(ev_rwlock_t *_lock attr_cleanup(_ev_rd_unlock) = _ev_rd_lock(lock), *_once = NULL; NULL == _once; _once = (void*)1) \
/* ev_with_rdlock end */
```

- `attr_cleanup(f)`在本项目中展开为`__attribute__((unused, cleanup(f)))`，这个属性用于变量，使变量生命周期结束时调用`f`，入参为指向这个变量的指针
- 在for循环中，首先定义了一个局部变量`_lock`，对传入`lock`调用`_ev_rd_lock`加锁后，将`lock`传给`_lock`；此外初始定义了`_once == NULL`
- 第一次循环，检查`NULL == _once`成立，进入循环体
- 第一次循环结束后，`_once = (void*)1`，此时不再满足循环条件，因此退出循环。这保证了使用该宏的代码只执行一次
- 循环正常退出时，`_lock`被销毁，自动调用`_ev_rd_unlock`函数解锁
- 若循环中使用了`return`或者`break`异常退出，`_lock`也会因为`for`的结束而销毁，此时自动调用`_ev_rd_unlock`函数解锁