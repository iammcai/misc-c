# device tree overlay

1. 编写dts文件
2. 编译为dtbo文件：`sudo dtc -@ -I dts -O dtb -o led_overlay.dtbo led_overlay.dts`
3. 放到启动目录：`sudo cp led_overlay.dtbo /boot/firmware/overlays`
4. 添加配置：`sudo echo "dtoverlay=led_overlay" | sudo tee -a /boot/firmware/config.txt`
5. 重启生效，或者执行`sudo dtoverlay led_overlay`来临时测试
6. 查看是否成功注入：`cat /proc/device-tree/my-led-device/status`，这里的my-led-device对应设备树里的名称

## device tree 语法

- `#address-cells`属性：子节点的`reg`中地址占多少个cell，一个cell是32bit
- `#size-cells`属性：子节点的`reg`中大小/长度占多少个cell