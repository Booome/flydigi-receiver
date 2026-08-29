# 星闪 SLE 协议分析与逆向可行性评估

## 一、背景

飞智八爪鱼5（Apex 5）的 2.4GHz 无线模式最初被假设为 Nordic ESB 或类似私有协议。
经过 FCC 认证文件分析和芯片拆解，确认手柄使用**星闪 (NearLink SLE 1.0)** 技术。

本文档汇总了完整的调研过程和结论，作为后续 BS21 开发的技术依据。

## 二、发现过程

### 2.1 nRF52840 方案的排除

最初计划使用 nRF52840 Dongle 的 2.4GHz radio 进行空中抓包，
基于以下假设：

1. 手柄可能使用 Nordic ESB（Enhanced ShockBurst）协议
2. nRF52840 radio 可以通过 promiscuous mode 捕获任意 2.4GHz 信号
3. 通过 RSSI 扫描 + raw radio 抓包可以逆向无线协议

**排除原因**：经 FCC 认证文件确认，手柄 2.4GHz 模式使用星闪 SLE 1.0，
其 PHY 层（Polar 码编码、中心调度、特定帧格式）与 Nordic radio 不兼容。
nRF52840 的 radio 只能解调 Nordic 兼容的 GFSK 信号。

### 2.2 FCC 认证文件分析

- **FCC ID**: 2AORE-K5
- **申请方**: Shanghai Flydigi Electronics Technology Co., Ltd.
- **认证日期**: 2025-05-20
- **频率范围**: 2402-2480 MHz
- **设备类型**: DSS (扩频) + DTS (数字传输) 复合设备
- **输出功率**: 0.5mW / 1.3mW / 3.1mW

### 2.3 内部芯片识别

通过 FCC 内部照片（文件 ID: 8306476）识别出两个射频芯片：

| 芯片标记 | 用途 | 技术 |
|---------|------|------|
| BP1Y303-D4 | BT/BLE | 蓝牙 (BR/EDR) |
| P352903N1 | 2.4G | 星闪 SLE 1.0 (NearLink) |

- P352903N1 无公开资料，为飞智定制 OEM 编号
- 产品销售页明确标注 "NearLink Technology with 1000Hz Ultra-High Polling Rate"
- 海思官网星闪技术介绍页确认 SLE 1.0 技术特性
- **无法确认具体芯片型号**：P352903N1 可能基于 Hi2821 (BS21E) 平台，但无公开 datasheet 验证

## 三、星闪 SLE 1.0 技术概述

### 3.1 星闪技术体系

星闪 (NearLink / SparkLink) 是中国自主的新一代短距离无线通信技术，
由国际星闪联盟制定标准，华为海思为主要芯片供应商。

- **SLB (SparkLink Long Band)**: 高速模式，类似 Wi-Fi
- **SLE (SparkLink Low Energy)**: 低功耗模式，类似 BLE 但性能更强

八爪鱼5使用的是 **SLE 1.0**。

### 3.2 SLE 1.0 关键特性

| 特性 | SLE 1.0 | BLE (对比) |
|------|---------|-----------|
| 最大带宽 | 4 MHz | 2 MHz |
| 最大物理层速率 | 12 Mbps | 2 Mbps |
| 最短帧长 (4M) | 49 us | 92 us (2M) |
| 最小收发间隔 | 248 us | 7.5 ms |
| 时延 | BLE 的 1/30 | - |
| 信道编码 | Polar 码 | - |
| 调度方式 | 中心调度 (C帧) | 分布式 |
| 接收灵敏度提升 | +7 dB @3Mbps | - |

### 3.3 SLE 无线帧类型

- **G帧 (GFSK帧)**: 广播/发现阶段使用，Type 1
- **T帧 (低时延帧)**: 连接后数据传输使用，Type 2
- 支持 1M/2M/4M 三种带宽
- 调制方式: GFSK / QPSK / 8PSK

## 四、SLE 协议架构与 API

### 4.1 G/T 角色模型

SLE 使用 G/T 角色模型（不同于 BLE 的 Central/Peripheral）：

| SLE 角色 | 含义 | 对应 BLE |
|---------|------|---------|
| G 节点 (Group head) | 中心/主设备 | Central/Master |
| T 节点 (Terminal) | 外设/从设备 | Peripheral/Slave |

角色可协商（`CAN_NEGO`）或不可协商（`NO_NEGO`）。

在八爪鱼5场景中：
- **手柄 = T 节点**（广播，等待连接）
- **接收器/dongle = G 节点**（扫描，发起连接）

### 4.2 SLE 发现与连接流程

```
T 节点（手柄）              G 节点（BS21 开发板）
──────────────              ──────────────────
sle_set_announce_data()     sle_set_seek_param()
sle_start_announce()        sle_start_seek()
                             ↓ seek_result_cb()
                             发现手柄 SLE 广播
                             ↓
                             sle_connect_remote_device(addr)
                             ↓ 连接回调
                             sle_pair_remote_device(addr)
                             ↓ 配对回调
                             SSAP 数据收发
```

### 4.3 BS21 SDK SLE API 清单

基于 Ai-BS21_SDK 头文件分析（`include/middleware/services/bts/sle/`）：

**设备管理**:
- `enable_sle()` / `disable_sle()`
- `sle_dev_manager_register_callbacks()`

**设备发现**:
- `sle_set_local_addr()` / `sle_get_local_addr()`
- `sle_set_local_name()` / `sle_get_local_name()`
- `sle_set_announce_data()` / `sle_start_announce()` / `sle_stop_announce()`
- `sle_set_seek_param()` / `sle_start_seek()` / `sle_stop_seek()`

**连接管理**:
- `sle_connect_remote_device(addr)` - G 节点发起连接
- `sle_disconnect_remote_device(addr)`
- `sle_get_connect_role(conn_id, &role)` - 查询 G/T 角色
- `sle_update_connect_param()`

**配对与绑定**:
- `sle_pair_remote_device(addr)` - 发起配对
- `sle_remove_paired_remote_device(addr)`
- `sle_get_pair_state(addr, &state)`
- `sle_set_nv_smp_keys()` - 设置 SMP 密钥

**物理层配置**:
- PHY 速率: `SLE_PHY_1M` / `SLE_PHY_2M` / `SLE_PHY_4M`
- 无线帧类型: Type 1/2，M序列 M0-M5
- 导频密度: 4:1 / 8:1 / 16:1

**广播模式** (`sle_announce_mode_t`):
- `NONCONN_NONSCAN` - 不可连接不可扫描
- `CONNECTABLE_NONSCAN` - 可连接不可扫描
- `NONCONN_SCANABLE` - 不可连接可扫描
- `CONNECTABLE_SCANABLE` - 可连接可扫描
- `CONNECTABLE_DIRECTED` - 定向连接（仅接受特定地址）

### 4.4 SLE 协议栈角色配置

BS21 SDK 的 Kconfig 支持以下 SLE 角色：

| Kconfig | 角色 | 说明 |
|---------|------|------|
| `SUPPORT_SLE_CENTRAL` | G 节点 | 纯 SLE 中心设备 |
| `SUPPORT_SLE_PERIPHERAL` | T 节点 | 纯 SLE 外设 |
| `SUPPORT_SLE_BLE_PERIPHERAL` | T 节点 | SLE + BLE 双模外设 |
| `SUPPORT_BLE_PERIPHERAL` | - | 仅 BLE 外设 |

本项目需要配置为 `SUPPORT_SLE_CENTRAL`（G 节点）。

## 五、逆向可行性评估

### 5.1 方案评估矩阵

| 方案 | 可行性 | 说明 |
|------|--------|------|
| nRF52840 radio 嗅探 | **不可行** | SLE PHY 与 Nordic radio 不兼容 |
| BS21 SLE 嗅探 (sniffer) | **不可行** | BS21 SDK 无 SLE sniffer/monitor 模式 |
| BS21 直接连接手柄 | **低概率** | 私有配对大概率阻止第三方连接 |
| BS21 伪装 dongle | **中低概率** | 需破解 SMP 密钥 |
| USB 抓包原装 dongle | **仅应用层** | 获取 NewXInput 协议，不含 SLE 无线信息 |
| SDR 抓包 2.4GHz | **理论可行** | 但 SLE Polar 码解调极其困难 |

### 5.2 BS21 直接连接手柄的障碍分析

1. **配对机制**：游戏手柄通常与原装 dongle 出厂预配对，
   手柄可能只接受已知 dongle 的连接请求
2. **定向广播**：手柄可能使用 `CONNECTABLE_DIRECTED` 模式，
   仅响应特定 SLE 地址
3. **SMP 密钥**：即使 SLE 连接建立，配对需要 SMP 密钥交换，
   没有原装 dongle 的密钥可能无法完成认证
4. **应用层协议**：即使连接成功，NewXInput 协议（5A A5 魔数、
   命令序列）需要额外逆向

### 5.3 推荐的渐进式策略

**阶段 1: SLE 扫描验证**（BS21 到货后立即可做）
- BS21 配置为 G 节点，执行 `sle_start_seek()`
- 手柄开机（PC模式），观察是否发现 SLE 广播
- 记录手柄 SLE 地址、广播数据、RSSI
- **成功标准**：能扫描到手柄的 SLE 广播

**阶段 2: 连接尝试**
- 基于扫描到的地址，尝试 `sle_connect_remote_device()`
- 观察连接是否建立、在哪个阶段被拒绝
- 如果连接建立，尝试 `sle_pair_remote_device()`
- **成功标准**：连接建立（即使配对失败）

**阶段 3: 数据收发**（如果阶段 2 成功）
- 尝试发送已知的 NewXInput 初始化命令
- 观察手柄响应
- 对比 USB 协议文档验证 payload 格式
- **成功标准**：收到手柄输入数据

**阶段 4: 完整接收器**（如果阶段 3 成功）
- 实现 USB HID 设备输出
- 替换 `sim_update()` 为真实 SLE 接收
- 完整的 controller_state 映射

### 5.4 备选方案

如果 BS21 无法直接连接手柄：

1. **拆解 dongle**：识别 dongle 内部 SLE 芯片，
   尝试通过 SWD/JTAG dump 固件，提取配对密钥
2. **USB 抓包**：用原装 dongle + PC usbmon 获取应用层数据格式
   （不含 SLE 无线协议，但了解 payload 结构）
3. **ESP32 BR/EDR**：通过蓝牙经典连接手柄（绕过 SLE）

## 六、开源项目参考

目前**没有任何公开项目逆向过星闪 SLE 无线协议**。
所有飞智手柄相关的开源项目都在 USB 层工作：

| 项目 | 方法 | 层级 |
|------|------|------|
| flydigi-vader5 | USB 协议文档 | USB HID |
| Flydigi5Pico | RP2350 USB Host->Device 桥接 | USB |
| openflydigi | PC hidraw 读写 | USB HID |

USB 协议已详细文档化（20字节标准报告 + 32字节扩展报告 5A A5 EF），
可作为 SLE payload 格式的参考。

## 七、参考链接

### 7.1 开源协议栈实现

- **OpenHarmony Nearlink Service**（星闪开源协议栈）
  - 主仓库: https://gitcode.com/openharmony/communication_nearlink_service
  - 镜像: https://github.com/openharmony/communication_nearlink_service
  - 本地路径: `~/workspace/communication_nearlink_service`
  - 内容: SSAP 协议实现、SLE 广播/扫描/连接管理、属性读写、通知机制
  - 用途: 作为 SLE 协议实现的参考，理解 SSAP 协议细节和状态机

### 7.2 芯片对比与瓶颈分析

#### 7.2.1 芯片信息

| | 飞智八爪鱼5 | 我们的 BS21 |
|---|---|---|
| **芯片标记** | P352903N1 | Hi2821 (BS21E) |
| **SLE 版本** | SLE 1.0 | SLE 1.0 |
| **固件** | 飞智定制 ROM | 标准 Ai-BS21_SDK |
| **公开资料** | 无 | 部分（SDK 文档） |

**注意**：P352903N1 的具体芯片型号无法从公开渠道验证。飞智可能使用海思 Hi2821 的定制 ROM 版本
或其他海思 SLE 芯片。由于是定制 OEM 编号，无公开 datasheet。

#### 7.2.2 WS63/WS65/BS21 硬件规格对比

| 规格 | **BS21E** | **WS63** | **WS65** |
|------|-----------|----------|----------|
| **CPU** | 64MHz RISC-V | 240MHz 32bit MCU | 未知（可能不存在） |
| **Flash** | 1MB | 4MB | 未知 |
| **RAM** | 160KB | 606KB SRAM + 300KB ROM | 未知 |
| **SLE 空口速率** | 12Mbps | 12Mbps (WS63E) / 4Mbps (WS63) | 未知 |
| **SLE 带宽** | 1M/2M/4M | 1M/2M/4M | 未知 |
| **SLE 回报率** | 2K-4K | 未知 | 未知 |
| **BLE** | 5.4 | 5.4 | 未知 |
| **Wi-Fi** | 不支持 | Wi-Fi 6 (802.11ax) | 未知 |
| **USB** | 支持 | 未提及 | 未知 |
| **雷达感知** | 不支持 | WS63E 支持 | 未知 |
| **应用场景** | 鼠标、键盘、消费电子 | 智能家电、物联网 | 未知 |
| **SDK 设备类型检测** | ❌ | ❌ | 未知 |
| **SDK 发布年份** | ~2023 | ~2024 | 未知 |

**关键差异**：
- **WS63 是 Wi-Fi + BLE + SLE 多模芯片**，BS21 仅 SLE + BLE
- **WS63 CPU 强得多**（240MHz vs 64MHz）
- **WS63 存储大得多**（4MB Flash + 606KB RAM vs 1MB Flash + 160KB RAM）
- **WS63 支持雷达感知**（WS63E 变体）
- **WS65 未找到公开信息**，可能不存在或未公开

#### 7.2.3 Flydigi 控制器芯片推断

基于硬件规格和行为分析：

| 可能性 | 芯片 | 理由 | 概率 |
|--------|------|------|------|
| 1 | **BS21E (Hi2821)** | 同一芯片平台，定制 ROM 修改默认行为 | **高** |
| 2 | **WS63** | 同为海思 SLE 芯片，但 SDK 行为不同 | 中 |
| 3 | **定制芯片** | 完全定制，与已知 SDK 无关 | 低 |

**最可能的情况**：Flydigi 使用 **BS21E 定制 ROM**，原因：
1. 同一芯片平台（Hi2821），硬件兼容
2. 定制 ROM 可以修改默认行为（如禁用 V10 格式）
3. 无需更换芯片，只需修改固件

**主要供应商**：
- **海思 (HiSilicon)**：星闪技术的主要推动者，提供 Hi2821 (BS21E) 等芯片
- **安信可 (Ai-Thinker)**：基于海思芯片的开发板和模组（如 Ai-BS21-32S-Kit）

**已知星闪芯片型号**：
| 芯片型号 | 说明 | 来源 |
|---------|------|------|
| Hi2821 (BS21E) | 海思 SLE 芯片，BS21 系列 | SDK、安信可文档 |
| WS63 | 另一款海思星闪芯片（见于 SDK 文档） | SDK 内部引用 |

**芯片获取难度**：
- 星闪芯片主要供应大厂（如飞智、华为生态链企业）
- 中小开发者难以直接采购裸片
- 通常通过开发板（如 Ai-BS21-32S-Kit）获取

#### 7.2.3 SDK 差距分析

| 能力 | 飞智定制 ROM | 标准 BS21 SDK |
|------|-------------|--------------|
| DLI 层访问 | ✅ 完全控制 | ❌ 无法访问 |
| 响应生成 | ✅ 定制逻辑 | ❌ ROM 内部处理 |
| SSAP hook | ✅ 可修改 | ❌ 不触发 |
| 原始 PDU 捕获 | ✅ 可访问 | ❌ 无法捕获 |

#### 7.2.4 瓶颈根因

**核心问题**：标准 BS21 SDK 是一个"黑盒"：
- 提供 SSAP client/server API
- 但底层 SLE 帧处理、响应生在 ROM 固件中
- 无法控制或观察底层行为

**飞智的定制 ROM 实现了特定的行为**，而标准 SDK 无法复制这些行为。

#### 7.2.5 SDK 获取难度

| SDK | 可用性 | 说明 |
|-----|--------|------|
| **Ai-BS21_SDK** | ✅ 公开 | 目前使用的 SDK，仅支持安信可开发板 |
| **fbb_bs2x（海思官方）** | ✅ 已克隆 | `~/workspace/fbb_bs2x`，HiSpark/GitCode |
| **OpenHarmony Nearlink** | ✅ 开源 | 参考实现，但需适配到 BS21 裸机环境 |

**fbb_bs2x 关键发现**：
- 官方 SDK **没有** CM_DEVTYPE_OLD/NEW 设备类型检测机制
- 使用简单的 `version = 1` 字段进行 exchange_info
- 开源 OpenHarmony 栈有更完善的设备类型自动检测

**结论**：SDK 获取不是瓶颈。官方 SDK 和开源栈的差异表明：
- 官方 SDK 是精简版，缺少设备类型检测
- 开源栈是完整版，有设备类型自适应
- 飞智可能使用定制协议栈或旧版 SDK

#### 7.2.6 可选路径

1. **接受限制**：在标准 SDK 能力范围内工作
2. **逆向飞智固件**：拆解原装 dongle，dump ROM 固件
3. **USB 抓包**：用原装 dongle + PC usbmon 获取应用层数据
4. **字节 patch**：通过逆向 ROM 函数，patch 关键行为（当前方案）

### 7.3 硬件与工具

- [FCC ID 2AORE-K5](https://fccid.io/2AORE-K5) - 八爪鱼5认证文件
- [海思星闪技术介绍](https://www.hisilicon.com/cn/techtalk/nearlink/introduction) - SLE 技术概述
- [国际星闪联盟开发者社区](https://developer.sparklink.org.cn/)
- [Ai-BS21_SDK](https://github.com/Ai-Thinker-Open/Ai-BS21_SDK) - BS21 开源 SDK
- [NearLink ToolBox](https://nearlink.docs.haohanyh.ovh/) - 星闪工具箱
- [安信可星闪模组文档](https://docs.ai-thinker.com/nearlink/) - BS21 模组资料
