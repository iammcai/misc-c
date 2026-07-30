# MISC-C LED

从LED开始练习驱动

## 器件说明

### 面包板

【内部连通规则】

- 同一行 a-e 五孔内部导通（如 1a-1b-1c-1d-1e）
- 同一行 f-j 五孔内部导通（如 1f-1g-1h-1i-1j）
- 中间沟槽两侧互不导通（适合跨插DIP芯片）
- 上下电源轨各自整条横向导通（部分型号中间断开，注意丝印标记）
- 不同行之间、左右半区之间均不导通

```
┌─────────────────────────────────────────────────────────────────────┐
│                        400孔面包板 (830mm × 55mm)                   │
├───┬──────────────────────────┬──┬──────────────────────────┬───┤
│   │  a b c d e               │  │  f g h i j               │   │
│ + ├─○ ○ ○ ○ ○───────────────┤  ├─○ ○ ○ ○ ○───────────────┤ + │ ← 电源轨(上)
│ - ├─○ ○ ○ ○ ○───────────────┤  ├─○ ○ ○ ○ ○───────────────┤ - │ ← 地线轨(上)
├───┼──────────────────────────┼──┼──────────────────────────┼───┤
│   │                          ║║                          │   │
│ 1 ├─○ ○ ○ ○ ○               ║║               ○ ○ ○ ○ ○─┤ 1 │
│ 2 ├─○ ○ ○ ○ ○               ║║               ○ ○ ○ ○ ○─┤ 2 │
│ 3 ├─○ ○ ○ ○ ○               ║║               ○ ○ ○ ○ ○─┤ 3 │
│ 4 ├─○ ○ ○ ○ ○               ║║               ○ ○ ○ ○ ○─┤ 4 │
│ 5 ├─○ ○ ○ ○ ○               ║║               ○ ○ ○ ○ ○─┤ 5 │
│ 6 ├─○ ○ ○ ○ ○    左半区      ║║     右半区    ○ ○ ○ ○ ○─┤ 6 │
│ 7 ├─○ ○ ○ ○ ○   (30列×5行)  ║║   (30列×5行)  ○ ○ ○ ○ ○─┤ 7 │
│ 8 ├─○ ○ ○ ○ ○               ║║               ○ ○ ○ ○ ○─┤ 8 │
│ 9 ├─○ ○ ○ ○ ○               ║║               ○ ○ ○ ○ ○─┤ 9 │
│10 ├─○ ○ ○ ○ ○               ║║               ○ ○ ○ ○ ○─┤10 │
│   │         ⋮                ║║          ⋮              │   │
│25 ├─○ ○ ○ ○ ○               ║║               ○ ○ ○ ○ ○─┤25 │
│26 ├─○ ○ ○ ○ ○               ║║               ○ ○ ○ ○ ○─┤26 │
│27 ├─○ ○ ○ ○ ○               ║║               ○ ○ ○ ○ ○─┤27 │
│28 ├─○ ○ ○ ○ ○               ║║               ○ ○ ○ ○ ○─┤28 │
│29 ├─○ ○ ○ ○ ○               ║║               ○ ○ ○ ○ ○─┤29 │
│30 ├─○ ○ ○ ○ ○               ║║               ○ ○ ○ ○ ○─┤30 │
├───┼──────────────────────────┼──┼──────────────────────────┼───┤
│ + ├─○ ○ ○ ○ ○───────────────┤  ├─○ ○ ○ ○ ○───────────────┤ + │ ← 电源轨(下)
│ - ├─○ ○ ○ ○ ○───────────────┤  ├─○ ○ ○ ○ ○───────────────┤ - │ ← 地线轨(下)
└───┴──────────────────────────┴──┴──────────────────────────┴───┘
```

### LED设备

使用的LED模块是一个三引脚LED模块，大概长这样：

```
|   💡  |
|       |
IN VCC GND
|   |   |
|   |   |
```

- VCC：输入电源，接3.3V
- GND：地脚
- IN：控制端，高电平有效，LED亮

## 设备树编写

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2712";

    fragment@0 {
        target-path = "/";                  /* 挂载到根节点，作为独立平台设备 */
        __overlay__ {
            miscc_led_label: miscc_led {    /* label: name */
                compatible = "miscc,led";   /* 必须与驱动 of_match_table 完全一致 */
                status = "okay";

                                            /* 属性名必须与驱动代码中读取的名称一致 */
                led-gpios = <&rp1_gpio 2 0>;

                default-state = "off";
            };
        };
    };
};
```

- `miscc_led_label: miscc_led`这一行，前者给别的dts节点引用，后者是节点名字，加载后会在`/proc/device-tree/`和`/sys/devices/platform/`目录下生成对应的目录
- `led-gpios = <&rp1_gpio 2 0>;`是在使用GPIO资源，格式：<控制器phandle 引脚号 flags>，树莓派5的控制器是`rpi_gpio`，`2`是GPIO2的BCM编号，通过`gpioinfo`获知，`0`表示`GPIO_ACTIVE_HIGH`，高电平有效

## 驱动编写

驱动编写分为几个部分：platform设备、misc字符设备

### misc设备

misc设备是内核提供的简化版字符设备框架。

标准的字符舍尔比需要手动申请主设备、次设备号，注册注销等。

misc有固定的主设备号，可以指定动态分配次设备号，注册注销也很方便，`/dev`节点自动创建

```C
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

static const struct file_operations led_fops = {
	.owner  = THIS_MODULE,
	.write  = led_write,
	.llseek = noop_llseek,
};
```

通过一个`fops`变量来指定一些文件操作的回调。`write`接口中：

- `struct led_priv *priv = container_of(file->private_data, struct led_priv, misc);`获取我们的管理变量，`file->private_data`是一个`struct miscdevice`变量，misc框架帮我们填充的

---

更规范的用法是使用`ioctl`接口，可以自己定义命令来进行用户态和内核程序的交互

用户态和驱动约定一组命令，`_IOW`和`_IOR`宏可以用来将输入参数组成一个完整的命令，包括了数据流向、设备魔数、命令序号和数据大小

`static long led_ioctl(struct file *file, unsigned int cmd, unsigned long arg)`入参分别是（1）文件的handler（2）ioctl命令（3）参数地址，是一个指针，指向用户空间，需要用`copy_from/to_user`来操作

```C

// IOCTL的命令号，led的ASCII
#define LED_IOCTL_MAGIC         (0x6C6564)
// 定义用户态往内核态写入的命令，_IOW宏用来生成Write方向命令
#define LED_IOCTL_SET_STATE     _IOW(LED_IOCTL_MAGIC, 0, int)
// 定义用户态从内核态读出的命令，_IOR宏用来生成Read方向命令
#define LED_IOCTL_GET_STATE     _IOR(LED_IOCTL_MAGIC, 1, int)

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
```

用户态调用belike

```C
int off = 0;
if(ioctl(g_led_fd, LED_IOCTL_SET_STATE, &off) < 0)
{
	safe_printf("-led: ioctl() fail.\n");
	return NULL;
}

int status = 0;
if(ioctl(g_led_fd, LED_IOCTL_GET_STATE, &status) < 0)
{
	dbg_error("get led gpio status fail");
	dbg_error("led module init fail");
	return;
}
```

### platform设备相关

在Linux内核中，Platform 设备（Platform Device）是一种专门用于描述 "非标准总线" 上硬件设备的抽象机制

简单来说，像USB、PCI、I2C、SPI这些设备都有各自成熟的总线协议和枚举机制（插上就能被自动发现）。但SoC内部集成的控制器（如GPIO、UART、定时器、DMA）以及直接挂在内存总线上的外设，没有这种"即插即用"的总线。Platform总线就是内核为这些"无家可归"的设备提供的一个虚拟统一管理框架

Platform驱动由三个独立部分组成，通过匹配机制绑定

|要素|	角色|	来源|	miscc_led驱动对应|
|---|---|---|---|
|Platform Bus	|虚拟总线，负责撮合设备与驱动|	内核内置，无需关心	|platform_bus_type|
|Platform Device	|描述硬件资源（寄存器地址、中断、GPIO等）|	设备树 (DTS/Overlay) 或板级代码|	miscc_led 节点|
|Platform Driver	|描述如何操作该硬件的业务逻辑|	驱动模块 (.ko)|	`led.ko`|

具体到驱动代码，需要做以下事情

- 实现设备的`probe`和`remove`回调
- 指定和设备树匹配的节点
- 设备注册到platform

#### `probe`

```C

// led设备描述
struct led_priv {
	struct gpio_desc *gpio;
	struct miscdevice misc;
};

// ...

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
	priv->misc.name  = "my_led";
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
```

接口的入参是Platform总线匹配成功后，传递给驱动的设备实例句柄，是内核封装好的一个设备对象，其中常用成员有：

- `&pdev->dev`：内嵌通用设备对象，所有`devm_ API`的第一个参数，用于资源生命周期绑定
- `pdev->dev.of_node`：指向对应设备树节点
- `platform_get_drvdata()`：获取私有数据指针，一般在`probe`存入，`remove/write`取出

这个接口在驱动和设备树绑定上时会进行调用，做了以下事情：

1. 动态创建`struct led_priv`类型的变量，用来后续在别的接口取出使用。`devm_`系列函数，可以在驱动卸载或者异常时自动释放资源，减少我们代码错误处理
2. 进行设备树的解析，获取字段。`devm_gpiod_get`进行gpio的申请
3. 创建misc字符设备，之后用来给用户读写接口。`priv->misc.name  = "my_led";`这一行决定了`/dev/`目录下的设备名字
4. `platform_set_drvdata`设置私有数据

#### `remove`

```C
static void led_remove(struct platform_device *pdev)
{
	struct led_priv *priv = platform_get_drvdata(pdev);

	misc_deregister(&priv->misc);
	dev_info(&pdev->dev, "LED driver removed\n");
}
```

当驱动和设备树解绑时会触发`remove`回调，其中进行了misc设备的注销


### 设备树绑定与注册

```C
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
```

1. `led_of_match`变量用来声明驱动和设备树的节点绑定，必须`{}`结尾否则会内存越界
2. `module_platform_driver(led_platform_driver);`是语法糖，展开后会注册`init`和`exit`函数，适用于入口出口只做`platform`设备注册的情况，简化代码