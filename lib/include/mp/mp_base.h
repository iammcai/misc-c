#ifndef __MP_BASE_H__
#define __MP_BASE_H__

// slab的枚举定义
typedef enum{
    SLAB_SIZE_8 = 0,
    SLAB_SIZE_16,
    SLAB_SIZE_32,
    SLAB_SIZE_64,
    SLAB_SIZE_128,
    SLAB_SIZE_256,
    SLAB_SIZE_512,
    SLAB_SIZE_1024,
    SLAB_SIZE_2048,
    SLAB_SIZE_4096,
    SLAB_SIZE_9216,     // 实际使用出现了jumbo帧，预留

    SLAB_SIZE_CNT,      // 计数
}slab_size_e;

#endif