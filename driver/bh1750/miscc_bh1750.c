// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) cai<sybstudy@yeah.net>.
 *
 * @file    bh1750.c
 * @brief   bh1750驱动代码，光照传感器
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-11-09
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-11-09 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/err.h>              // for IS_ERR, PTR_ERR
#include <linux/iio/iio.h>          // for iio sub sys

#include "miscc_bh1750.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 关闭设备
#define BH1750_CMD_POWER_DOWN   (0x00)
// 使能设备
#define BH1750_CMD_POWER_ON     (0x01)
// 复位设备
#define BH1750_CMD_RESET        (0x07)
// 连续高精度模式测量使能
#define BH1750_CMD_HRM_START    (0x10)
// 单次高精度模式测量
#define BH1750_CMD_OT_HRM       (0x20)

// 从机地址
#define BH1750_SLAVE_ADDR       (0x23)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// bh1750结构体
struct bh1750_data{
    struct i2c_client *client;
    struct mutex lock;
    u8 rx_buf[2];               // 接收数据
};

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       probe func for bh1750
 * 
 * @param[in]   client
 */
static int bh1750_probe(struct i2c_client *client);

/**
 * @brief       remove func for bh1750
 * 
 * @param[in]   client
 */
static void bh1750_remove(struct i2c_client *client);

/**
 * @brief       read raw func for iio subsys
 * 
 * @param[in]   indio_dev
 * @param[in]   chan
 * @param[in]   val
 * @param[in]   val2
 * @param[in]   mask
 */
static int bh1750_read_raw(
    struct iio_dev *indio_dev,
    struct iio_chan_spec const *chan,
    int *val,
    int *val2,
    long mask
);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 设备树匹配表
static const struct of_device_id bh1750_of_match[] = {
    {.compatible = "miscc,bh1750"},
    {}
};
MODULE_DEVICE_TABLE(of, bh1750_of_match);

// i2c驱动结构体
static struct i2c_driver bh1750_i2c_driver = {
    .driver = {
        .name = "miscc_bh1750",
        .of_match_table = bh1750_of_match,
    },
    .probe = bh1750_probe,
    .remove = bh1750_remove,
};
module_i2c_driver(bh1750_i2c_driver);

// IIO通道说明
static const struct iio_chan_spec bh1750_channels[] = {
    {
        .type = IIO_LIGHT,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | 
                              BIT(IIO_CHAN_INFO_SCALE),
    },
};

// IIO信息
static const struct iio_info bh1750_iio_info = {
    .read_raw = bh1750_read_raw,
};

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static int bh1750_probe(struct i2c_client *client)
{
    struct bh1750_data *data;
    u8 cmd = BH1750_CMD_RESET;
    int ret;
    struct iio_dev *indio_dev;

    // 分配设备内存、设置私有数据
    indio_dev = devm_iio_device_alloc(&client->dev, sizeof(struct iio_dev));
    if(!indio_dev)
        return -ENOMEM;
    data = iio_priv(indio_dev);
    data->client = client;
    mutex_init(&data->lock);
    i2c_set_clientdata(client, indio_dev);

    // 上电
    cmd = BH1750_CMD_POWER_ON;
    ret = i2c_master_send(client, &cmd, 1);
    if(1 != ret)
        return dev_err_probe(&client->dev, -EIO, "power on for bh1750 failed.\n");

    // 设置复位
    cmd = BH1750_CMD_RESET;
    ret = i2c_master_send(client, &cmd, 1);
    if(1 != ret)
        return dev_err_probe(&client->dev, -EIO, "soft reset for bh1750 failed.\n");

    // 设置连续高精度模式测量
    cmd = BH1750_CMD_HRM_START;
    ret = i2c_master_send(client, &cmd, 1);
    if(1 != ret)
        return dev_err_probe(&client->dev, -EIO, "set high res mode for bh1750 failed.\n");

    // 配置iio属性
    indio_dev->name = BH1750_IIO_DEVICE_NAME;
    indio_dev->info = &bh1750_iio_info;
    indio_dev->channels = bh1750_channels;
    indio_dev->num_channels = ARRAY_SIZE(bh1750_channels);
    indio_dev->modes = INDIO_DIRECT_MODE;
    // 注册到IIO核心
    ret = devm_iio_device_register(&client->dev, indio_dev);
    if (ret)
        return dev_err_probe(&client->dev, ret, "failed to register IIO device\n");

    dev_info(&client->dev, "BH1750 probed successfully\n");

    return 0;
}

static void bh1750_remove(struct i2c_client *client)
{
    // 设置下电
    u8 cmd = BH1750_CMD_POWER_DOWN;
    int ret = i2c_master_send(client, &cmd, 1);
    if(ret != 1)
        dev_err(&client->dev, "BH1750 set power down fail\n");

    dev_info(&client->dev, "BH1750 removed.\n");
}

static int bh1750_read_raw(
    struct iio_dev *indio_dev,
    struct iio_chan_spec const *chan,
    int *val,
    int *val2,
    long mask
)
{
    struct bh1750_data *data = iio_priv(indio_dev);
    int ret;

    if (chan->type != IIO_LIGHT)
        return -EINVAL;

    switch(mask)
    {
        case IIO_CHAN_INFO_RAW:
            mutex_lock(&data->lock);
            ret = i2c_master_recv(data->client, data->rx_buf, 2);
            mutex_unlock(&data->lock);
            if (ret != 2)
                return -EIO;
            // 拼接原始值
            *val = ((u16)data->rx_buf[0] << 8) | data->rx_buf[1];
            return IIO_VAL_INT;

        case IIO_CHAN_INFO_SCALE:       // raw * scale = real value
            // Lux = Raw / 1.2 = Raw * (10 / 12)
            *val = 10;
            *val2 = 12;
            return IIO_VAL_FRACTIONAL;

        default:
            return -EINVAL;
    }
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cai");
MODULE_DESCRIPTION("BH1750 i2c driver for RPi5 matching bh1750_overlay.dts");
