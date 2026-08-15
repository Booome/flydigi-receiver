# Flydigi Receiver Project

## 项目状态

手柄使用**星闪 SLE 1.0 (NearLink)** 进行 2.4GHz 无线通信，非 Nordic ESB。
nRF52840 Dongle 的 radio 不兼容 SLE PHY，无法用于无线接收。
**Ai-BS21-32S-Kit**（BS21 开发板，2块）已到货，可开始 SLE 开发。

详细分析见：
- `docs/sle-analysis.md` - SLE 协议分析与逆向可行性评估
- `docs/bs21-development.md` - BS21 开发板、SDK 与开发路线图
- `docs/controller-modes.md` - 手柄模式与协议详解

## 平台

### nRF52840 Dongle（已有，M0-M3 已完成）

nRF52840 固件（`wireless/`）实现了 USB CDC + UART + formatter 输出管道，
但 **radio 不适用于 SLE 通信**。保留为输出管道的参考实现。

编译和烧录方法参见 `docs/build-and-flash.md`。

关键要点：
- 使用 `make -C wireless build` / `make -C wireless flash` 命令（固件），`make -C wireless build-blinky` 等（NCS 示例），不要直接调用 west
- NCS 环境通过 `nrfutil sdk-manager toolchain launch` 隔离注入，不要全局设置 LD_LIBRARY_PATH
- 编译时需设置 `ZEPHYR_BASE=~/ncs/v3.4.0/zephyr`（Makefile 已处理），因为本项目不在 NCS workspace 内
- nRF52840 Dongle 命令行烧录需要先生成 DFU zip 包（`nrfutil nrf5sdk-tools pkg generate`），不能直接烧录 .hex/.elf（Programmer app GUI 可以直接烧 ELF）
- Dongle 进入 DFU 模式：按侧面 Reset 键

### BS21 开发板（已到货）

Ai-BS21-32S-Kit，基于 Hi2821 (BS21) 芯片：
- SLE 1.0 + BLE 5.4 + USB 2.0
- 双 Type-C：USB1 原生 USB 2.0（HID/CDC），USB2 CH340 串口（烧录/调试）
- SDK: Ai-BS21_SDK (GitHub) / XFusion (Linux 构建) / HiSilicon fbb_bs2x (官方)
- 开发环境搭建和路线图见 `docs/bs21-development.md`

## 手柄硬件信息

- 型号：飞智八爪鱼5 (Flydigi Apex 5)
- FCC ID：2AORE-K5
- 2.4GHz 芯片：P352903N1（星闪 SLE 1.0，飞智定制编号）
- 蓝牙芯片：BP1Y303-D4（BR/EDR）
- USB VID/PID：0x37D7 / 0x2501

## 串口识别（双 CDC ACM）

nRF52840 固件使用双 CDC ACM 虚拟串口，通过 USB 接口描述符字符串区分：

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
