// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) cai<sybstudy@yeah.net>.
 *
 * @file    led.c
 * @brief   led驱动代码
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-07-30
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-07-28 | cai | Initial creation.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include "miscc_led_ioctl.h"

// led设备描述
struct led_priv {
	struct gpio_desc *gpio;
	struct miscdevice misc;
};

/* ==================== 字符设备操作 ==================== */

#if 0
static ssize_t led_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct led_priv *priv = container_of(file->private_data, struct led_priv, misc);
	char kbuf[8];
	int val;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	if (kbuf[0] == '1' || !strncmp(kbuf, "on", 2))
		val = 1;
	else if (kbuf[0] == '0' || !strncmp(kbuf, "off", 3))
		val = 0;
	else
		return -EINVAL;

	gpiod_set_value(priv->gpio, val);
	return count;
}
#endif

/**
 * @brief		ioctl cb
 * 
 * @param[in]	file		- file handler
 * @param[in]	cmd			- ioctl cmd
 * @param[in]	arg			- input arg
 * 
 * @retval		error code
 */
static long led_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct led_priv *priv = container_of(file->private_data, struct led_priv, misc);
	int val;

	switch(cmd)
	{
		case LED_IOCTL_SET_STATE:		// 设置led状态
			if(copy_from_user(&val, (int __user *)arg, sizeof(val)))
				return -EFAULT;
			gpiod_set_value(priv->gpio, !!val);
			break;
		case LED_IOCTL_GET_STATE:		// 获取led状态
			val = gpiod_get_value(priv->gpio);
			if(val < 0)			// 硬件读取失败
				return val;
			if (copy_to_user((int __user *)arg, &val, sizeof(val)))
				return -EFAULT;
			break;
		default:
			return -ENOTTY;
	}

	return 0;
}

// file operations
static const struct file_operations led_fops = {
	.owner  = THIS_MODULE,
	.unlocked_ioctl = led_ioctl,
	.llseek = noop_llseek,
};

/* ==================== Platform 驱动核心 ==================== */

static int led_probe(struct platform_device *pdev)
{
	struct led_priv *priv;
	const char *state_str;
	enum gpiod_flags flags;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/*
	 * 解析 DTS 中的 default-state 属性
	 * "on" → GPIOD_OUT_HIGH, "off"/缺失 → GPIOD_OUT_LOW
	 */
	flags = GPIOD_OUT_LOW; /* 默认值，对应 default-state = "off" */
	if (device_property_read_string(&pdev->dev, "default-state", &state_str) == 0)
	{
		if (!strcmp(state_str, "on"))
			flags = GPIOD_OUT_HIGH;
		else if (!strcmp(state_str, "keep"))
			flags = GPIOD_ASIS;
		/* "off" 或其他值保持默认的 GPIOD_OUT_LOW */
	}

	/*
	 * "led" 自动匹配 DTS 中的 "led-gpios"
	 * flags 由 default-state 动态决定，不再硬编码
	 */
	priv->gpio = devm_gpiod_get(&pdev->dev, "led", flags);
	if (IS_ERR(priv->gpio))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->gpio),
				     "Failed to get LED GPIO\n");

	priv->misc.minor = MISC_DYNAMIC_MINOR;
	priv->misc.name  = "miscc_led";
	priv->misc.fops  = &led_fops;
	priv->misc.parent = &pdev->dev;

	ret = misc_register(&priv->misc);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to register misc device\n");

	platform_set_drvdata(pdev, priv);
	dev_info(&pdev->dev, "LED probed (/dev/my_led), default-state=%s\n",
		 state_str ? state_str : "off");
	return 0;
}

static void led_remove(struct platform_device *pdev)
{
	struct led_priv *priv = platform_get_drvdata(pdev);

	misc_deregister(&priv->misc);
	dev_info(&pdev->dev, "LED driver removed\n");
}

// 匹配设备树节点
static const struct of_device_id led_of_match[] = {
	{ .compatible = "miscc,led" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, led_of_match);	// 自动加载，设备树类型of

static struct platform_driver led_platform_driver = {
	.probe  = led_probe,
	.remove = led_remove,
	.driver = {
		.name           = "gpio-led",		// 回退匹配名称，sysfs路径名
		.of_match_table = led_of_match,		// 匹配表挂载
	},
};

module_platform_driver(led_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cai");
MODULE_DESCRIPTION("Platform LED driver for RPi5 matching led_overlay.dts");
