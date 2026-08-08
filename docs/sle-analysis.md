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

- [FCC ID 2AORE-K5](https://fccid.io/2AORE-K5) - 八爪鱼5认证文件
- [海思星闪技术介绍](https://www.hisilicon.com/cn/techtalk/nearlink/introduction) - SLE 技术概述
- [国际星闪联盟开发者社区](https://developer.sparklink.org.cn/)
- [Ai-BS21_SDK](https://github.com/Ai-Thinker-Open/Ai-BS21_SDK) - BS21 开源 SDK
- [XFusion 开发文档](https://www.coral-zone.cc/document/zh_CN/) - Linux BS21 构建工具
- [NearLink ToolBox](https://nearlink.docs.haohanyh.ovh/) - 星闪工具箱
- [安信可星闪模组文档](https://docs.ai-thinker.com/nearlink/) - BS21 模组资料
