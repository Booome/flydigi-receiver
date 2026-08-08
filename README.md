# 飞智八爪鱼5 自制接收器项目

## 项目目标

逆向工程飞智八爪鱼5 (Flydigi Apex 5) 的无线协议，实现自制接收器。

## 硬件环境

| 设备 | 型号 | 用途 |
|------|------|------|
| 手柄 | 飞智八爪鱼5 (Flydigi Apex 5) | 被逆向的目标设备 |
| nRF52840 Dongle | 已有 | M0-M3 输出管道参考（radio 不兼容 SLE） |
| BS21 开发板 | Ai-BS21-32S-Kit ×2 | **星闪 SLE 开发**（已采购，待到货） |
| ESP32 | 原版，支持 BR/EDR | Classic BT 连接手柄（搁置） |
| 电脑 | Arch Linux (KDE Plasma Wayland) | 开发和调试 |

## 关键发现

手柄 2.4GHz 无线模式使用**星闪 SLE 1.0 (NearLink)**，非 Nordic ESB：
- FCC ID: 2AORE-K5，内部 SLE 芯片 P352903N1
- nRF52840 radio 不兼容 SLE PHY（Polar 码），无法接收信号
- BS21 芯片原生支持 SLE 1.0 + USB 2.0，适合做接收器

详细分析见 [SLE 协议分析](docs/sle-analysis.md)。

## 项目结构

```
flydigi-receiver/
├── ble/            # BLE/BR-EDR 方案（搁置，仅文档）
│   └── docs/
├── wireless/       # nRF52840 输出管道（M0-M3 已完成）
│   ├── firmware/   # Zephyr RTOS 固件
│   ├── tests/      # 单元测试 + PC 端脚本
│   └── Makefile    # build / flash / test
├── docs/           # 全局文档
│   ├── sle-analysis.md       # SLE 协议分析与可行性评估
│   ├── bs21-development.md   # BS21 开发板与路线图
│   ├── controller-modes.md   # 手柄模式与协议详解
│   └── build-and-flash.md    # nRF52840 编译烧录指南
├── Makefile        # 顶层入口
└── AGENTS.md
```

## 开发状态

### nRF52840 输出管道（M0-M3，已完成）

通用输出基础设施，BS21 项目可参考复用：
- USB CDC ACM 双串口（Debug + Data）
- 文本/二进制格式化器（带单元测试）
- UART TTL 输出后端

### 星闪 SLE 逆向（M5-M8，待物料到货）

| 里程碑 | 目标 | 状态 |
|--------|------|------|
| M5 | SLE 扫描验证（BS21 扫描手柄广播） | 待物料 |
| M6 | SLE 连接尝试 | 待 M5 |
| M7 | 数据收发与协议解析 | 待 M6 |
| M8 | USB HID 输出 | 待 M7 |

路线图详见 [BS21 开发文档](docs/bs21-development.md)。

### 蓝牙方案（搁置）

手柄蓝牙模式使用 BR/EDR（经典蓝牙），非 BLE。需 ESP32 开发。
详见 [BLE 设计文档](ble/docs/design.md)。

## 相关开源项目

- [flydigi-vader5](https://github.com/BANANASJIM/flydigi-vader5) - USB 协议文档
- [Flydigi5Pico](https://github.com/ruomox/Flydigi5Pico) - RP2350 USB 桥接
- [openflydigi](https://github.com/mkaliaha/openflydigi) - HID 命令协议逆向
- [Ai-BS21_SDK](https://github.com/Ai-Thinker-Open/Ai-BS21_SDK) - BS21 星闪 SDK
- [XFusion](https://www.coral-zone.cc/document/zh_CN/) - Linux BS21 构建工具

## 编译与烧录（nRF52840）

详见 [docs/build-and-flash.md](docs/build-and-flash.md)。

```bash
make -C wireless build          # 编译固件
make -C wireless flash          # 烧录固件（需 Dongle DFU 模式）
make -C wireless build-test     # 编译单元测试
make -C wireless run-test       # 运行单元测试
```
