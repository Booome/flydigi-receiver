# Flydigi Receiver Project

## 编译与烧录

编译和烧录方法参见 `docs/build-and-flash.md`。

关键要点：
- 使用 `make -C wireless build` / `make -C wireless flash` 命令（固件），`make -C reference build-blinky` 等（示例），不要直接调用 west
- NCS 环境通过 `nrfutil sdk-manager toolchain launch` 隔离注入，不要全局设置 LD_LIBRARY_PATH
- 编译时需设置 `ZEPHYR_BASE=~/ncs/v3.4.0/zephyr`（Makefile 已处理），因为本项目不在 NCS workspace 内
- nRF52840 Dongle 命令行烧录需要先生成 DFU zip 包（`nrfutil nrf5sdk-tools pkg generate`），不能直接烧录 .hex/.elf（Programmer app GUI 可以直接烧 ELF）
- Dongle 进入 DFU 模式：按侧面 Reset 键

## 串口识别（双 CDC ACM）

固件使用双 CDC ACM 虚拟串口，通过 USB 接口描述符字符串区分：

| 接口字符串 | 用途 | 内容 |
|-----------|------|------|
| `Flydigi-Debug` | 调试输出 | printk / LOG |
| `Flydigi-Data` | 功能数据 | formatter 输出（文本/二进制） |

`/dev/ttyACM*` 编号不稳定（取决于 USB 枚举顺序），**不要**靠编号区分。
通过 sysfs 接口字符串识别：

```bash
# 查看某个 ttyACM 的接口字符串
cat /sys/class/tty/ttyACM0/device/interface
```

测试程序自动扫描示例（Python）：

```python
import glob, os

def find_port(target_iface):
    for dev in glob.glob('/dev/ttyACM*'):
        link = f'/sys/class/tty/{os.path.basename(dev)}/device/interface'
        try:
            if open(link).read().strip() == target_iface:
                return dev
        except FileNotFoundError:
            pass
    return None

data_port  = find_port('Flydigi-Data')
debug_port = find_port('Flydigi-Debug')
```
