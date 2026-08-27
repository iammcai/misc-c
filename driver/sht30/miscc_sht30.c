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
 *   1.0 | 2026-08-24 | cai | Conver IIO to Hwmon
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/hwmon.h>          // for hwmon
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>            // for IS_ERR, PTR_ERR
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
 * @brief       is_visible callback
 * 
 * @param[in]   type        - input type
 * @param[in]   attr        - attr
 * 
 * @retval      mode of file
 */
static umode_t sht30_hwmon_is_visible(const void *data, enum hwmon_sensor_types type, u32 attr, int channel)
{
    if((type == hwmon_temp && attr == hwmon_temp_input) || 
        (type == hwmon_humidity && attr == hwmon_humidity_input))
    {
        return 0444;    // 只读
    }

    return 0;       // 不创建文件
}

/**
 * @brief       read callback
 * 
 * @param[in]   dev     - device
 * @param[in]   type    - type
 * @param[in]   attr    - attr
 * @param[in]   channel - channel
 * @param[out]  val     - val
 */
static int sht30_hwmon_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, long *val);

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

/* sht30 hwmon操作集合 */
static const struct hwmon_ops sht30_hwmon_ops = {
    .is_visible = sht30_hwmon_is_visible,
    .read = sht30_hwmon_read,
};

/* sht30支持的通道信息 */
static const struct hwmon_channel_info* sht30_hwmon_channel_info[] = {
    HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),        // 支持温度
    HWMON_CHANNEL_INFO(humidity, HWMON_H_INPUT),    // 支持湿度
    NULL,   // sential
};

/* sht30信息集合，用于注册 */
static const struct hwmon_chip_info sht30_chip_info = {
    .ops = &sht30_hwmon_ops,
    .info = sht30_hwmon_channel_info,
};

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

static int sht30_hwmon_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, long *val)
{
    struct sht30_data *data = dev_get_drvdata(dev);
    int temp, humi, ret;

    if(attr != hwmon_temp_input && attr != hwmon_humidity_input)
        return -EOPNOTSUPP;

    /* 测量 */
    ret = sht30_measure(data, &temp, &humi);
    if(ret)
        return ret;

    /* 根据传感器类型，返回对应的值 */
    switch(type)
    {
        case hwmon_temp:
            *val = temp;  /* 温度值，单位是毫摄氏度 (m°C) */
            return 0;
        case hwmon_humidity:
            *val = humi;  /* 湿度值，单位是千分比 (m%)，表示 0.001% */
            return 0;
        default:
            return -EOPNOTSUPP;
    }
}

static int sht30_probe(struct i2c_client *client)
{
    struct sht30_data *data;
    u8 reset_cmd[2];
    int ret;
    struct device *hwmon_dev;

    // 检查I2C适配器是否支持基本功能
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
        return -EOPNOTSUPP;

    // 分配设备内存以及设置私有数据
    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;
    data->client = client;
    mutex_init(&data->lock);

    // probe时复位
    reset_cmd[0] = SHT30_CMD_SOFT_RESET >> 8;
    reset_cmd[1] = SHT30_CMD_SOFT_RESET & 0xff;
    ret = i2c_master_send(client, reset_cmd, 2);
    if (ret != 2)
        return dev_err_probe(&client->dev, -EIO, "soft reset failed\n");
    msleep(2);      // 手测指出最多1ms

    // 注册hwmon设备
    hwmon_dev = devm_hwmon_device_register_with_info(&client->dev,
                                                     SHT30_HWMON_DEVICE_NAME,
                                                     data,
                                                     &sht30_chip_info,
                                                     NULL);
    if (IS_ERR(hwmon_dev))
        return dev_err_probe(&client->dev, PTR_ERR(hwmon_dev),
                             "failed to register hwmon device\n");

    dev_info(&client->dev, "SHT30 probed successfully\n");
    return 0;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cai");
MODULE_DESCRIPTION("SHT30 Hwmon Driver");
MODULE_SOFTDEP("pre: hwmon");        // 模块依赖