以下是使用 `perf` 定位性能瓶颈的完整命令流程，按操作顺序整理：

---

## 1. 安装 perf（树莓派）

```bash
# 查看内核版本
uname -r

# 尝试安装
sudo apt update
sudo apt install linux-tools-raspi -y
# 或
sudo apt install linux-perf-<版本号> -y   # 例如 linux-perf-5.10
```

如果安装后运行 `perf --version` 报错，手动编辑 `/usr/bin/perf` 脚本，将 `exec "perf_$version" "$@"` 中的 `$version` 改为实际安装的版本号。

---

## 2. 编译程序（保留调试符号）

```bash
# 在 Makefile 的 CFLAGS 中添加 -g
CFLAGS = -g -O2 ...
make clean
make
```

---

## 3. 录制采样数据

```bash
sudo perf record -F 999 -g ./Main
```

- `-F 999`：采样频率（可调，99/1999 等）
- `-g`：记录调用栈
- 生成 `perf.data` 文件

如果程序运行时间很短，可增加测试循环次数或让程序 sleep 后手动附加：

```bash
# 先运行程序，获取 PID，然后附加
sudo perf record -F 999 -g -p <PID>
# 按 Ctrl+C 停止录制
```

---

## 4. 查看宏观性能计数器（辅助判断）

```bash
sudo perf stat -e cycles,instructions,cache-references,cache-misses,L1-dcache-load-misses ./Main
```

对比 slab 版和 sys 版，判断是否为缓存或指令数问题。

---

## 5. 查看热点函数（`perf report`）

### 交互式查看：
```bash
sudo perf report
```
- 上下键浏览
- `+` 展开调用链
- `a` 查看某函数的汇编级热点（`perf annotate`）

### 输出文本报告：
```bash
sudo perf report -n --stdio > perf_report.txt
```

---

## 6. 查看单个函数的汇编级热点（`perf annotate`）

### 直接查看某个函数：
```bash
sudo perf annotate -s _mp_slab_node_get
```

### 保存到文件：
```bash
sudo perf annotate -s _mp_slab_node_get > annotate_full.txt
```

### 只显示有采样百分比的指令（过滤）：
```bash
sudo perf annotate -s _mp_slab_node_get --stdio | grep -E '^[[:space:]]+[0-9]+\.[0-9]+' > annotate_filtered.txt
```

### 如果 `perf.data` 不在当前目录，指定路径：
```bash
sudo perf annotate -i /path/to/perf.data -s _mp_slab_node_get
```

---

## 7. 查看符号是否存在（确认函数名）

```bash
sudo perf report --stdio | grep _mp_slab_node_get
```

---

## 8. 其他有用命令

### 查看所有支持的事件：
```bash
sudo perf list
```

### 实时监控当前进程（类似 top）：
```bash
sudo perf top -p <PID>
```

### 生成火焰图（需安装 FlameGraph 脚本）：
```bash
sudo perf script | ./FlameGraph/stackcollapse-perf.pl | ./FlameGraph/flamegraph.pl > flame.svg
```

---

## 9. 权限设置（避免 `perf_event_paranoid` 限制）

```bash
sudo sh -c "echo -1 > /proc/sys/kernel/perf_event_paranoid"
sudo sh -c "echo 0 > /proc/sys/kernel/kptr_restrict"
```

---

## 10. 总结排查流程

1. `perf record` 采样
2. `perf stat` 看宏观指标
3. `perf report` 定位热点函数
4. `perf annotate -s <函数名>` 查看具体指令
5. 根据指令占比优化代码
6. 重复 1~5 验证改进

如果遇到函数名无法解析，检查编译时是否加 `-g`，或使用 `objdump -t Main | grep _mp_slab_node_get` 确认符号名。