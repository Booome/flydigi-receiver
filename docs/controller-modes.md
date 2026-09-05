# 飞智八爪鱼5 — 模式详解 (PC / Switch / Android / iOS)

## 一、概述

八爪鱼5支持三种物理连接方式：USB有线、2.4GHz无线（接收器）、蓝牙。
手柄背面有一个**物理拨动开关**，用于切换连接方式：

```
背面拨动开关位置：
┌──────────┬──────────┬──────────┐
│   左侧   │   中间   │   右侧   │
│  PC模式  │ 蓝牙模式 │  (因型号) │
│ (2.4G)  │ (BT)     │          │
└──────────┴──────────┴──────────┘
     │           │
     ▼           ▼
   白色LED      蓝色LED
```

**关键理解**：八爪鱼5本身并没有独立的 "Switch模式"、"Android模式"、"iOS模式" 协议层。
真正的区分是：

| 拨动开关 | 连接方式 | 无线协议 | 应用层协议 | 目标平台 |
|---------|---------|---------|-----------|---------|
| 左侧 (PC) | 2.4G接收器 / USB有线 | **星闪 SLE 1.0** | 私有NewXInput | PC (Windows/Linux) |
| 中间 (蓝牙) | 蓝牙 (BR/EDR) | BR/EDR | 标准HID | Android / iOS / Switch / PC蓝牙 |

> **重要更正**：经 FCC 认证文件（FCC ID: 2AORE-K5）和内部拆解照片确认，
> 2.4GHz 模式使用**星闪 (NearLink SLE 1.0)** 而非传统 2.4GHz 私有协议。
> 蓝牙模式使用 BR/EDR（经典蓝牙），而非之前认为的 BLE。
> 详见 `docs/sle-analysis.md`。

也就是说，**Android、iOS、Switch 都走蓝牙同一条路**，区别主要在于操作系统如何识别和处理这个HID设备，而不是手柄端的协议不同。

---

## 二、PC模式 (2.4G / USB有线)

### 认证信息

- **FCC ID**: 2AORE-K5
- **认证日期**: 2025-05-20
- **频率范围**: 2402-2480 MHz
- **设备类型**: DSS (扩频) + DTS (数字传输) 复合设备
- **输出功率**: 0.5mW / 1.3mW / 3.1mW（三个功率级别）

### 内部射频芯片

经 FCC 内部照片确认，手柄内部有两个射频芯片：

| 芯片标记 | 用途 | 技术 |
|---------|------|------|
| BP1Y303-D4 | BR/EDR（经典蓝牙单模） | 蓝牙 (BR/EDR) |
| P352903N1 | 2.4G | **星闪 SLE 1.0** (NearLink) |

> P352903N1 为飞智定制编号，无公开资料。产品页标注 "NearLink Technology with 1000Hz Ultra-High Polling Rate"，
> FCC 认证文件中频率范围 (2402-2480 MHz) 与星闪 SLE 一致。
>
> **注意**：P352903N1 的具体芯片型号无法从公开渠道验证。飞智可能使用海思 Hi2821 (BS21E)
> 的定制 ROM 版本或其他海思 SLE 芯片。由于是定制 OEM 编号，无公开 datasheet。

### 连接方式
- **2.4G无线**：通过飞智专用USB接收器
- **USB有线**：Type-C直连

### 协议特征
- **无线协议**：星闪 SLE 1.0（NearLink），非 Nordic ESB，非 BLE
- **应用层协议**：NewXInput（飞智自定义的XInput变体）
- USB VID：`0x37D7`（飞智自己的Vendor ID）
- USB PID：`0x2501`（八爪鱼5）/ `0x2401`（黑武士5 Pro）
- 接口类型：Vendor HID（Usage Page `0xFFA0`，Class `0xFF`，SubClass `0x5D`）
- 数据包魔数：`5A A5`

### USB接口布局

| 接口 | 类型 | 端点 | 用途 |
|------|------|------|------|
| 0 | Vendor (0xFF/0x5D) | EP1 IN 20B, EP5 OUT 8B | 标准输入 + 震动 |
| 1 | HID | EP2 IN 32B, EP6 OUT 32B | 配置命令 + 扩展输入 |
| 2 | HID | EP2 IN 32B | （未使用）|
| 3 | HID | EP4 IN 64B | 键盘模式 |

### 标准输入报告 (20字节，接口0)

```
偏移  大小  说明
0     1    Report ID (0x00)
1     1    子类型 (0x14 = 2.4G)
2     1    杂项: 十字键 + Start/Select/L3/R3
3     1    按键: LB/RB/Home + ABXY
4     1    左扳机 LT (0-255)
5     1    右扳机 RT (0-255)
6-7   2    左摇杆 X (int16 LE)
8-9   2    左摇杆 Y (int16 LE)
10-11 2    右摇杆 X (int16 LE)
12-13 2    右摇杆 Y (int16 LE)
14-19 6    保留
```

### 扩展输入报告 (32字节，接口1，需开启"第三方控制"标志)

```
魔数头: 5A A5 EF (3字节)

偏移  大小  说明
0-2   3    魔数 (5A A5 EF)
3-4   2    左摇杆 X (int16 LE)
5-6   2    左摇杆 Y (int16 LE)
7-8   2    右摇杆 X (int16 LE)
9-10  2    右摇杆 Y (int16 LE)
11    1    按键1 (十字键 + A/B/Select/X)
12    1    按键2 (Y/Start/LB/RB/L3/R3)
13    1    扩展按键 (C/Z/M1-M4/LM/RM)
14    1    扩展按键2 (O/Home)
15    1    左扳机 (0-255)
16    1    右扳机 (0-255)
17-18 2    陀螺仪 X (int16 LE)
19-20 2    陀螺仪 Y (int16 LE)
21-22 2    陀螺仪 Z (int16 LE)
23-24 2    加速度 X (int16 LE, 4096=1g)
25-26 2    加速度 Y (int16 LE)
27-28 2    加速度 Z (int16 LE)
29-31 3    保留
```

### "第三方控制"标志 (Command 17)

这是PC模式下最关键的软件开关：
- 通过飞智空间站APP或命令`0x11`开启
- 开启后手柄切换到**原始数据模式**
- `controller_data = False`（停止标准XInput报告）
- `raw_data = True`（启用带5A A5 EF头的扩展报告）
- **SDL/Steam需要这个标志才能正确识别手柄**

### IMU传感器数据
- 陀螺仪：±2000°/s 范围
- 加速度计：4096 = 1g
- 有线模式采样率：~970 Hz
- 2.4G模式采样率：~295 Hz（SLE 1.0 实际可达 1000Hz 回报率，但 USB 端报告速率受 dongle 限制）

### 关键命令

| 命令 | ID | 用途 |
|------|-----|------|
| GetDeviceInfo | 0x10 | 获取设备类型、固件版本、电量 |
| GetDongleVersion | 0x11 | 获取接收器版本 |
| ThirdPartyControl | 0x11 | 开启/关闭第三方控制模式 |
| Haptic/Rumble | 0x12 | 左右马达震动 |
| Acquire | 0x1C | 声明手柄控制权（SDL发送"SDL"标签）|
| SetForceTrigger | 0x81 | 自适应扳机效果（赛车/狙击/后坐力/锁定）|
| ProfileSwitch | 0xA2 | 切换配置档案（最多4个）|

---

## 三、蓝牙模式 (Android / iOS / Switch)

### 连接方式
- 蓝牙（背面开关拨到中间，蓝色LED）
- 设备名：如 `Flydigi VADER3P`（因型号而异）
- 蓝牙VID：`0xD7D7`（与USB的`0x37D7`不同）
- 蓝牙PID：如 `0x0041`

> **注意**：经实测，八爪鱼5的蓝牙模式使用 **BR/EDR（经典蓝牙）**，
> 而非 BLE HID over GATT。设备识别为 "Xbox Wireless Controller"，
> UUID 0x1124。之前文档中描述的 BLE 内容可能不适用于此手柄。

### M10 实测广播信息（BR/EDR inquiry）

2026-09-05 用 `bluetooth/esp32-wroom-32e/apps/bt_scan/`（ESP-IDF v6.0.2 + `esp_hid_scan`）扫描手柄 BT 模式（蓝色 LED 亮 + 配对状态）得到：

| 字段 | 值 |
|---|---|
| MAC | `b5:5d:e7:98:54:75` |
| NAME | `Xbox Wireless Controller`（BR/EDR 侧就是这个名，**不**是"Flydigi Apex5"）|
| COD | major = PERIPHERAL (5)，minor = 2（gamepad/joystick）|
| UUID | 0x1124（HID over BR/EDR L2CAP，**经典蓝牙标准**，不是 GATT）|
| RSSI | -48 ~ -58 dBm（距离 ~0.5m）|
| 广播通道 | **仅 BR/EDR**：40s 扫描期内 BLE GAP debug 一次都没出现这个 MAC；BLE 扫描到的全是 Apple 设备（`U-ACGDDEC` 等）和一些 random UUID 0xfdaa 的设备 |

**结论**：八爪鱼5 的蓝牙模式 **只走经典蓝牙 BR/EDR**，**不广播 BLE**。BP1Y303-D4 是单模 BR/EDR 蓝牙芯片（不是 BR/EDR + BLE 双模）。本节原先"一部分走 BLE、一部分走经典蓝牙"的猜测**有误**。

**捕获命令复现**（在 `.env` 已设 `BOARD_A_TYPE=esp32-wroom-32e` 的前提下）：

```bash
source /opt/esp-idf/export.sh
cd bluetooth/esp32-wroom-32e
python3 tools/build.py --app bt_scan
python3 tools/burn.py --app bt_scan
python3 ../../tools/capture_uart.py --board-a --rst-a --duration 40 --odir /tmp --ts
grep "Xbox Wireless Controller\|mode=BR_EDR" /tmp/board_a_<ts>.log
```

### 协议：标准 HID over BR/EDR

### GATT服务结构

> 以下为基于 BLE 规范的描述，实际八爪鱼5使用 BR/EDR，GATT 结构可能不完全适用。
> 待后续 ESP32 BR/EDR 开发验证。

```
GAP Service (0x1800)
├── 设备名称
└── 外观

HID Service (0x1812) ★ 核心
├── HID Information
├── Report Map         ★ 定义输入格式
├── Report (Input)     ★ 实际按键/摇杆数据
├── Report (Output)    ★ 震动命令
├── Protocol Mode
└── HID Control Point

Battery Service (0x180F)
└── Battery Level

飞智自定义服务 (私有UUID) ★ 扩展功能
├── 力反馈扳机控制
├── RGB灯效控制
├── 宏录制/回放
├── 配置同步（通过飞智空间站APP）
└── 陀螺仪数据（部分型号）
```

### 报告描述符 (Report Map)

> 以下为基于 BLE 规范的描述，实际八爪鱼5使用 BR/EDR，报告格式可能不同。
> 待后续验证。

标准HID描述符，定义手柄布局：
- 按键、十字键、模拟摇杆、扳机
- 按键映射与XInput一致（ABXY、Bumper、Trigger、摇杆）
- **没有飞智私有扩展**（无5A A5魔数、无原始数据模式）

### 蓝牙模式的局限性

| 特性 | PC模式 (星闪SLE/有线) | 蓝牙模式 (BR/EDR) |
|------|-------------------|---------|
| 私有命令接口 | ✅ 5A A5 协议 | ❌ 不可用 |
| 自适应扳机 | ✅ 支持 | ❌ 硬件限制 |
| 原始数据模式 | ✅ 可切换 | ❌ 不需要 |
| 震动 | ✅ 支持 | ⚠️ 部分型号不支持 |
| IMU数据 | ✅ 内置在扩展报告中 | ⚠️ 仅通过自定义服务（未验证）|
| 功耗 | 较高 | 极低 |
| 延迟 | 10-30ms | 3-10ms |
| 连接速度 | 较慢（2-3秒） | 快（<1秒）|
| 配对 | 需要配对 | 通常不需要PIN码 |

---

## 四、各平台的具体表现

### Android

**连接方式**：蓝牙（BR/EDR，与iOS/Switch相同协议）

- 标准HID输入，映射到Android Gamepad API
- 无特殊Android专属协议 - 手柄就是一个通用蓝牙HID游戏手柄
- 飞智空间站APP可通过自定义蓝牙服务进行配置同步

**2.4G接收器在Android上的问题**：
- 接收器使用Vendor HID类，Android原生不识别
- 需要root用户态驱动（如[vader5-pro-android](https://github.com/LARo-developer/vader5-pro-android)项目）
- 原理：读取`/dev/hidraw`的私有32字节报告，通过`/dev/uinput`重新映射为Xbox Elite手柄

### iOS

**连接方式**：蓝牙（BR/EDR）

- 标准HID，不需要MFi认证
- iOS的Game Controller框架原生支持标准蓝牙HID游戏手柄
- 基本按键/摇杆/扳机功能可直接使用
- **注意**：飞智自定义服务（力反馈扳机、RGB灯效等）在iOS上未验证

### Switch (Nintendo Switch)

**连接方式**：蓝牙（BR/EDR）

- 飞智手柄**不模拟Switch Pro手柄**（不像一些第三方手柄那样）
- 使用标准蓝牙HID协议连接
- Switch系统对第三方蓝牙HID手柄的支持有限
- **注意**：Switch模式的兼容性需要实际测试验证，部分手柄可能需要Switch端的蓝牙适配器

---

## 五、模式切换总结

### 硬件切换（物理开关）

```
背面拨动开关：
左侧 = PC模式 (星闪SLE接收器/USB有线)
中间 = 蓝牙模式 (BR/EDR HID)
```

### 软件切换（DInput/XInput切换）

在蓝牙模式下，部分型号可通过按键组合切换DInput/XInput：

**黑武士3 (Vader 3) 示例**：
- **XInput模式**（白色灯）：同时长按 Circle + X 约3秒
- **DInput模式**（蓝色灯）：同时长按 Circle + A 约3秒
- 反复执行相同组合即可来回切换

**八爪鱼5**：具体按键组合需要实际测试验证。

### 第三方控制标志（PC模式内部切换）

```
飞智空间站APP → Command 17
├── thirdPartyControl = 1 → 手柄进入原始报告模式
│   ├── controller_data = False (停止标准XInput报告)
│   ├── raw_data = True (启用5A A5 EF扩展报告)
│   └── SDL/Steam需要此模式才能正确识别手柄
└── thirdPartyControl = 0 → 手柄返回标准模式
    ├── controller_data = True
    └── raw_data = False
```

> **注意**：以上为基于 USB 协议的分析。星闪 SLE 无线模式的应用层协议
> 是否与 USB 一致，需要通过 BS21 开发板连接手柄后验证。

---

## 六、命令编号对照表

| 功能 | XInput模式 (SLE/有线) | DInput模式 (USB有线) |
|------|------------------|---------------------|
| 获取设备信息 | `0x10` (16) | `0xEC` (236) |
| 获取接收器版本 | `0x11` (17) | `0x11` (17) |
| 读取配置 | `0x21` (33) | `0xEB` (235) |
| 读取LED配置 | `0x26` (38) | `0xE5` (229) |
| 写入配置确认 | `0x23`/`0x25` | `0xEA`/`0xE7`/`0x33` |

**重要**：XInput和DInput模式的命令编号完全不同，必须先确认当前模式才能正确发送命令。

---

## 七、数据格式对比

### 标准XInput报告（无第三方标志）
- 20字节，接口0
- Report ID: 0x00，子类型: 0x14
- 标准Xbox按键映射
- 8位扳机，16位摇杆
- **无扩展按键**（M1-M4、C、Z、LM、RM）
- **无IMU数据**

### 扩展/原始报告（开启第三方标志）
- 32字节，接口1
- 魔数头：5A A5 EF
- 全部标准按键 + 扩展按键（M1-M4、C、Z、LM、RM、Home、Circle）
- 8位扳机，16位摇杆
- IMU数据：3轴陀螺仪 + 3轴加速度计（12字节）
- 共29字节有效数据

> 以上为 USB 端报告格式。SLE 无线端 payload 格式待验证。

### 蓝牙HID报告（BR/EDR）

> 以下为基于 BLE 规范的描述，实际八爪鱼5使用 BR/EDR，报告格式待验证。

---

## 八、对自制接收器的意义

1. **PC模式是主要逆向目标**：星闪 SLE 1.0 无线协议 + NewXInput 应用层协议
2. **蓝牙模式**：使用 BR/EDR（经典蓝牙），非 BLE，详见 `docs/sle-analysis.md`
3. **命令编号因模式而异**：接收器必须正确处理XInput/DInput两套命令
4. **"第三方控制"标志是关键**：SDL/Steam集成必须支持这个开关
5. **初始化序列**：连接后需要发送特定命令序列：
   ```
   5a a5 01 02 03  (设备信息)
   5a a5 a1 02 a3  (MAC/序列号)
   5a a5 02 02 04  (配置读取)
   5a a5 04 02 06  (配置数据)
   ```

---

## 九、参考资料

1. [openflydigi PROTOCOL.md](https://github.com/mkaliaha/openflydigi/blob/main/PROTOCOL.md) — 最详细的协议文档
2. [flydigi-vader5 docs/protocol.md](https://github.com/BANANASJIM/flydigi-vader5/blob/main/docs/protocol.md) — 2.4G接收器协议
3. [Flydigi5Pico README](https://github.com/ruomox/Flydigi5Pico/blob/main/README.md) — USB桥接架构
4. [vader3 README](https://github.com/ahungry/vader3/blob/main/README.md) — 蓝牙DInput/XInput切换
5. [SDL HIDAPI Flydigi驱动](https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_flydigi.c) — 官方输入处理
6. [openflydigi findings-other-devices.md](https://github.com/mkaliaha/openflydigi/blob/main/docs/findings-other-devices.md) — 设备代码和协议选择
7. [openflydigi findings-steam.md](https://github.com/mkaliaha/openflydigi/blob/main/docs/findings-steam.md) — 第三方控制标志和SDL集成
