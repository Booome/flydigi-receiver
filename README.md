# 飞智八爪鱼5 自制接收器项目

## 项目目标

逆向工程飞智八爪鱼5 (Flydigi Apex 5) 的无线协议，实现自制接收器。

## 硬件环境

| 设备 | 型号 | 用途 |
|------|------|------|
| 手柄 | 飞智八爪鱼5 (Flydigi Apex 5) | 被逆向的目标设备 |
| 开发板 | nRF52840 Dongle | 2.4GHz 无线接收器 |
| 蓝牙模块 | ESP32（原版，支持 BR/EDR） | Classic BT 连接手柄（搁置） |
| 电脑 | Arch Linux (KDE Plasma Wayland) | 开发和调试 |

## 项目结构

```
flydigi-receiver/
├── ble/            # BLE 方案（搁置，仅文档）
│   └── docs/
├── wireless/       # 2.4GHz 无线方案（当前重点）
│   ├── firmware/   # nRF52840 固件（Zephyr RTOS）
│   ├── tests/      # 单元测试 + PC 端脚本
│   └── build/      # 构建输出
├── reference/      # NCS 示例验证（blinky/cdc）
├── docs/           # 全局文档
├── Makefile        # 编译/烧录/测试入口
└── AGENTS.md
```

## 开发状态

### 蓝牙方案（搁置）

经实测，八爪鱼5 手柄**不支持 BLE HID over GATT**，只支持 Bluetooth Classic (BR/EDR)。
nRF52840 仅支持 BLE，无法直接连接手柄。已采购 ESP32（支持 BR/EDR）用于后续开发。

详见 [BLE 设计文档](ble/docs/design.md)。

### 2.4GHz 无线方案（当前重点）

使用 nRF52840 的 2.4GHz radio 接收八爪鱼5 的 2.4GHz 闪玩模式信号。
需逆向飞智私有 2.4GHz 协议。

### 已完成的通用基础（M0-M3）

- USB CDC ACM 双串口（Debug + Data）
- 文本/二进制格式化器（带单元测试）
- UART TTL 输出后端
- 编译/烧录/测试工具链

## 相关开源项目

- [flydigi-vader5](https://github.com/BANANASJIM/flydigi-vader5) - 飞智黑武士5 Pro Linux 驱动，含 2.4G 协议逆向
- [Flydigi5Pico](https://github.com/ruomox/Flydigi5Pico) - RP2350 协议桥接，含完整协议分析
- [bluepad32](https://github.com/ricardoquesada/bluepad32) - ESP32 蓝牙手柄 Host，参考架构

## 编译与烧录

详见 [docs/build-and-flash.md](docs/build-and-flash.md)。

```bash
make build          # 编译固件
make flash          # 烧录固件（需 Dongle DFU 模式）
make build-test     # 编译单元测试
make run-test       # 运行单元测试
```
