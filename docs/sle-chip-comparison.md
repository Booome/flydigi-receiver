# 星闪芯片规格对比

## 一、芯片概览

| 特性 | BS21 (Hi2821) | WS53 (WS53V100) | WS63 |
|------|--------------|-----------------|------|
| **厂商** | 海思 (HiSilicon) | 海思 (HiSilicon) | 海思 (HiSilicon) |
| **系列** | BS2X 系列 | WS53 系列 | WS63 系列 |
| **定位** | 鼠标、键盘、测距、消费电子 | Wi-Fi + BLE + SLE Combo 物联网 | Wi-Fi 6 + BLE + SLE Combo 物联网 |
| **CPU** | RISC-V 32-bit | RISC-V 32-bit | RISC-V 32-bit |
| **CPU 主频** | 64 MHz | 未明确（≤240MHz） | 240 MHz |
| **Flash** | 1 MB | 4 MB | 4 MB |
| **RAM** | 160 KB | 未明确 | 606 KB SRAM + 300 KB ROM |
| **SLE 版本** | SLE 1.0 | SLE 1.0 | SLE 1.0 |
| **SLE 带宽** | 1M/2M/4M | 1M/2M/4M | 1M/2M/4M |
| **空口速率** | 12 Mbps | 12 Mbps | 4 Mbps (WS63) / 12 Mbps (WS63E) |
| **BLE** | BLE 5.4 | BLE 5.4 | BLE 5.4 |
| **Wi-Fi** | 不支持 | Wi-Fi 4 (802.11b/g/n) | Wi-Fi 6 (802.11b/g/n/ax) |
| **USB** | USB 2.0 | 未明确 | 支持 |
| **测距** | 支持 | 未明确 | 支持 |
| **雷达感知** | 不支持 | 不支持 | WS63E 支持 |

## 二、详细规格

### 2.1 BS2X 系列（BS20/BS21E/BS22）

来源：`fbb_bs2x/README.md`

| 规格 | BS20 | BS21E | BS22 |
|------|------|-------|------|
| **CPU 主频** | 64 MHz | 64 MHz | 64 MHz |
| **Flash** | 1 MB | 1 MB | 1 MB |
| **RAM** | 128 KB | 160 KB | 160 KB |
| **SLE RAM** | 1 KB | 2 KB | 4 KB |
| **空口速率** | 4 Mbps | 12 Mbps | 12 Mbps |
| **USB** | 支持 | 支持 | 支持 |
| **有线回报率** | 1K | 4K | 8K |
| **测距** | 支持 | 支持 | 支持 |

**应用场景**：鼠标、键盘、测距、电子消费类

**SDK 特点**：
- 搭载 LiteOS 系统
- 闭源协议栈（libbth_gle.a）
- 无设备类型检测（CM_DEVTYPE_OLD/NEW）
- 默认 V10 响应格式

### 2.2 WS53V100

来源：`fbb_ws53` 文档

| 规格 | 说明 |
|------|------|
| **封装** | QFN52 (6mm × 6mm) |
| **CPU** | RISC-V 32-bit |
| **Flash** | 4 MB |
| **SLE** | SLE 1.0，1M/2M/4M 带宽，12 Mbps |
| **BLE** | BLE 5.4，1M/2M 带宽，2 Mbps |
| **Wi-Fi** | 802.11b/g/n |
| **SDIO** | 支持（最高 50MHz） |
| **晶体** | 32.768 kHz RTC，32 MHz 主时钟 |

**应用场景**：大小家电、电工照明等物联网智能场景

**SDK 特点**：
- 搭载系统未明确
- 无设备类型检测（与 BS2X 类似）

### 2.3 WS63 系列

来源：`fbb_ws63` 文档

| 规格 | WS63 | WS63E |
|------|------|-------|
| **CPU** | RISC-V 32-bit，240 MHz | 同左 |
| **SRAM** | 606 KB | 同左 |
| **ROM** | 300 KB | 同左 |
| **Flash** | 4 MB（内置） | 同左 |
| **SLE** | SLE 1.0，4 Mbps | SLE 1.0，12 Mbps |
| **BLE** | BLE 5.4，2 Mbps | 同左 |
| **Wi-Fi** | Wi-Fi 6 (802.11ax)，150 Mbps | 同左 |
| **雷达感知** | 不支持 | 支持 |
| **ADC** | 6 路 | 同左 |
| **PWM** | 8 路 | 同左 |
| **UART** | 3 个 | 同左 |
| **GPIO** | 19 个 | 同左 |
| **电源电压** | 3.3V / 5V | 同左 |
| **外部晶体** | 24 MHz / 40 MHz | 同左 |

**应用场景**：
- WS63：智能家电、电工照明
- WS63E：支持雷达人体活动检测（常电类物联网）

**SDK 特点**：
- 支持 OpenHarmony
- 无设备类型检测（与 BS2X 类似）

## 三、SDK 协议栈对比

| 特性 | BS2X SDK | WS53 SDK | WS63 SDK | 开源栈 |
|------|----------|----------|----------|--------|
| **SLE 支持** | ✅ | ✅ | ✅ | ✅ |
| **SSAP 协议** | ✅ | ✅ | ✅ | ✅ |
| **exchange_info** | ✅ | ✅ | ✅ | ✅ |
| **设备类型检测** | ❌ | ❌ | ❌ | ✅ |
| **自动格式切换** | ❌ | ❌ | ❌ | ✅ |
| **默认响应格式** | V10 | V10（推断） | V10（推断） | 自动切换 |
| **版本函数** | `version_unpack` | 类似 | `version_unpack`, `get_version_capability` | 完整 |
| **OS** | LiteOS | 未明确 | OpenHarmony | OpenHarmony |

## 四、与真机行为的匹配度

### 4.1 真机观察到的行为

| 行为 | 真机（飞智） | BS2X SDK | WS63 SDK |
|------|-------------|----------|----------|
| **Find 响应格式** | `05 03`（主路径） | `05 0b 00 87`（V10） | V10（推断） |
| **设备类型** | 类似 NEW | 固定 OLD | 固定 OLD |

### 4.2 推断

- **所有官方 SDK 都使用 V10 格式**，无法直接匹配真机的行为
- 真机使用主路径格式（`05 03`），与开源栈的 NEW 设备行为一致
- Flydigi 可能使用：
  1. **定制版 SDK**（修改了默认行为）
  2. **完整版协议栈**（有设备类型检测）
  3. **完全不同的实现**

### 4.3 芯片型号无法确定

- **BS21**：用于鼠标/键盘，64 MHz，1 MB Flash，最简规格
- **WS53**：Wi-Fi + BLE + SLE，更高规格
- **WS63**：Wi-Fi 6 + BLE + SLE，最高规格

由于官方 SDK 行为相似（都无设备类型检测），**无法通过行为反推芯片型号**。

## 五、结论

1. **芯片选型不是瓶颈**：BS21 是正确的芯片方向（SLE 1.0）
2. **瓶颈在于 SDK**：所有官方 SDK 都是精简版，缺少设备类型检测
3. **真机行为需要定制 SDK**：飞智可能使用定制版或完整版协议栈
4. **需要固件分析**：要确认芯片型号和协议栈实现，需要 dump 真机固件

## 六、参考链接

- BS2X SDK：`/home/bodong/workspace/fbb_bs2x`
- WS53 SDK：`/home/bodong/workspace/fbb_ws53`
- WS63 SDK：`/home/bodong/workspace/fbb_ws63`
- 开源协议栈：`/home/bodong/workspace/communication_nearlink_service`
- 安信可 SDK：`~/.local/Ai-BS21_SDK`
