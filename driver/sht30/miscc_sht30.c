// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) cai<sybstudy@yeah.net>.
 *
 * @file    miscc_sht30.c
 * @brief   sht30驱动代码
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-07-31
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-07-31 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include "miscc_sht30.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// sht30从机地址
#define SHT30_SLAVE_ADDR                (0x44)

// sht30操作命令
#define SHT30_CMD_MEASURE_HIGH          (0x2400)        // no clock strecthing
#define SHT30_CMD_SOFT_RESET            (0x30a2)
#define SHT30_CMD_STATUS                (0xf32d)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

struct sht30_data{
    struct i2c_client *client;
    struct mutex lock;
    u8 tx_buf[2] __aligned(I2C_SMBUS_BLOCK_MAX);
    u8 rx_buf[6] __aligned(I2C_SMBUS_BLOCK_MAX);
};

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       sht30 crc
 * 
 * @param[in]   data
 * @param[in]   data_len
 * 
 * @retval      crc checksum
 */
static u8 _sht30_crc8(const u8 *data, int len)
{
    u8 crc = 0xff;
    while(len --)
    {
        crc ^= *data++;
        for(int i = 0; i < 8; i++)
            crc = (crc & 0x80) ? (crc << 1)^0x31 : crc << 1;
    }
    return crc;
}

/**
 * @brief       IIO read_raw回调
 * 
 * @param[in]   indio_dev       - iio子系统对应设备
 * @param[in]   chan            - iio通道的信息
 * @param[out]  val             - 用于描述不同数据格式
 * @param[out]  val2            - 用于描述不同数据格式
 * @param[in]   mask            - 掩码，读取数据的类型
 * 
 * @retval      表示数据类型的宏
 */
static int sht30_read_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan, int *val, int *val2, long mask);

/**
 * @brief       probe func for sht30
 * 
 * @param[in]   client  - i2c client data
 * 
 * @retval      error code
 */
static int sht30_probe(struct i2c_client *client);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// IIO通道声明
static const struct iio_chan_spec sht30_channels[] = {
    {
        .type = IIO_TEMP,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
    },
    {
        .type = IIO_HUMIDITYRELATIVE,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
    },
};

// IIO回调
static const struct iio_info sht30_iio_info = {
    .read_raw = sht30_read_raw,
};

/* 设备树匹配表 */
static const struct of_device_id sht30_of_match[] = {
    { .compatible = "miscc,sht30" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sht30_of_match);

/* I2C 驱动结构体 */
static struct i2c_driver sht30_driver = {
    .driver = {
        .name = "miscc_sht30",
        .of_match_table = sht30_of_match,
    },
    .probe = sht30_probe,
};
module_i2c_driver(sht30_driver);

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

/**
 * @brief       measure temp and humi
 * 
 * @param[in]   data    - sht30 data
 * @param[out]  temp    - 温度
 * @param[out]  humi    - 湿度
 * 
 * @note        进行单次高重复性测量
 */
static int sht30_measure(struct sht30_data *data, int *temp, int *humi)
{
    int ret;
    u16 raw_temp, raw_humi;

    mutex_lock(&data->lock);

    // 发送测量的命令
    data->tx_buf[0] = SHT30_CMD_MEASURE_HIGH >> 8;
    data->tx_buf[1] = SHT30_CMD_MEASURE_HIGH & 0xff;
    ret = i2c_master_send(data->client, data->tx_buf, 2);
    if(ret != 2)
    {
        ret = -EIO;
        goto done;
    }

    // 非clock stretching，等待数据准备完成，datasheet指出最大15ms
    msleep(16);

    // 读取结果，6B，| temp MSB | temp LSB | crc | humi MSB | humi LSB | crc |
    ret = i2c_master_recv(data->client, data->rx_buf, 6);
    if(ret != 6)
    {
        ret = -EIO;
        goto done;
    }

    // crc校验
    if((_sht30_crc8(&data->rx_buf[0], 2) != data->rx_buf[2]) || (_sht30_crc8(&data->rx_buf[3], 2) != data->rx_buf[5]))
    {
        dev_err(&data->client->dev, "crc check failed\n");
        ret = -EIO;
        goto done;
    }

    // 原始数据转换为物理值
    raw_temp = (data->rx_buf[0] << 8) | data->rx_buf[1];
    raw_humi = (data->rx_buf[3] << 8) | data->rx_buf[4];

    dev_info(&data->client->dev, "RAW: temp=0x%04x(%d), humi=0x%04x(%d)\n", raw_temp, raw_temp, raw_humi, raw_humi);

    // 整体乘以1000
    *temp = -45000 + (175000LL * (int)raw_temp) / 65535;
    *humi = (100000LL * (int)raw_humi) / 65535;

    ret = 0;

done:
    mutex_unlock(&data->lock);
    return ret;
}

static int sht30_read_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan, int *val, int *val2, long mask)
{
    struct sht30_data *data = iio_priv(indio_dev);      // 从iio私有数据获取struct sht30_data
    int temp, humi, ret;

    switch(mask)
    {
        case IIO_CHAN_INFO_RAW:     // 读取原始值
            ret = sht30_measure(data, &temp, &humi);
            if(ret)
                return ret;
            *val = (chan->type == IIO_TEMP) ? temp : humi;      // 根据获取的类型来选取
            return IIO_VAL_INT;
        case IIO_CHAN_INFO_SCALE:       // 读取比例值，此时真实值为 RAW * (*val) / (*val2)
            *val = 1;
            *val2 = 1000;
            return IIO_VAL_FRACTIONAL;
        default:
            return -EINVAL;
    }
}

static int sht30_probe(struct i2c_client *client)
{
    struct iio_dev *indio_dev;
    struct sht30_data *data;
    u8 reset_cmd[2];
    int ret;

    // 检查I2C适配器是否支持基本功能
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
        return -EOPNOTSUPP;

    // 分配IIO设备以及设置私有数据
    indio_dev = devm_iio_device_alloc(&client->dev, sizeof(struct sht30_data));
    if(!indio_dev)
        return -ENOMEM;
    data = iio_priv(indio_dev);
    data->client = client;
    mutex_init(&data->lock);

    // probe时复位
    reset_cmd[0] = SHT30_CMD_SOFT_RESET >> 8;
    reset_cmd[1] = SHT30_CMD_SOFT_RESET & 0xff;
    ret = i2c_master_send(client, reset_cmd, 2);
    if (ret != 2)
        return dev_err_probe(&client->dev, -EIO, "soft reset failed\n");
    msleep(2);      // 手测指出最多1ms

    // 设置IIO设备属性，并注册
    indio_dev->name = SHT30_IIO_DEVICE_NAME;
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->channels = sht30_channels;
    indio_dev->num_channels = ARRAY_SIZE(sht30_channels);
    indio_dev->info = &sht30_iio_info;
    ret = devm_iio_device_register(&client->dev, indio_dev);
    if (ret)
        return dev_err_probe(&client->dev, ret, "failed to register IIO device\n");

    dev_info(&client->dev, "SHT30 probed successfully\n");
    return 0;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cai");
MODULE_DESCRIPTION("SHT30 IIO Driver");
MODULE_SOFTDEP("pre: industrialio");        // 模块依赖