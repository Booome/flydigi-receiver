# 飞智八爪鱼5 — 模式详解 (PC / Switch / Android / iOS)

## 一、概述

八爪鱼5支持三种物理连接方式：USB有线、2.4GHz无线（接收器）、蓝牙BLE。
手柄背面有一个**物理拨动开关**，用于切换连接方式：

```
背面拨动开关位置：
┌──────────┬──────────┬──────────┐
│   左侧   │   中间   │   右侧   │
│  PC模式  │ 蓝牙模式 │  (因型号) │
│ (2.4G)  │ (BLE)    │          │
└──────────┴──────────┴──────────┘
     │           │
     ▼           ▼
   白色LED      蓝色LED
```

**关键理解**：八爪鱼5本身并没有独立的 "Switch模式"、"Android模式"、"iOS模式" 协议层。
真正的区分是：

| 拨动开关 | 连接方式 | 协议 | 目标平台 |
|---------|---------|------|---------|
| 左侧 (PC) | 2.4G接收器 / USB有线 | 私有NewXInput | PC (Windows/Linux) |
| 中间 (蓝牙) | BLE蓝牙 | 标准BLE HID over GATT | Android / iOS / Switch / PC蓝牙 |

也就是说，**Android、iOS、Switch 都走蓝牙BLE同一条路**，区别主要在于操作系统如何识别和处理这个BLE HID设备，而不是手柄端的协议不同。

---

## 二、PC模式 (2.4G / USB有线)

### 连接方式
- **2.4G无线**：通过飞智专用USB接收器
- **USB有线**：Type-C直连

### 协议特征
- 协议名称：**NewXInput**（飞智自定义的XInput变体）
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
- 2.4G模式采样率：~295 Hz

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
- BLE蓝牙（背面开关拨到中间，蓝色LED）
- 设备名：如 `Flydigi VADER3P`（因型号而异）
- 蓝牙VID：`0xD7D7`（与USB的`0x37D7`不同）
- 蓝牙PID：如 `0x0041`

### 协议：标准 BLE HID over GATT

蓝牙模式下，八爪鱼5是一个**标准BLE HID设备**，不使用飞智私有协议。

### GATT服务结构

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

标准HID描述符，定义手柄布局：
- 按键、十字键、模拟摇杆、扳机
- 按键映射与XInput一致（ABXY、Bumper、Trigger、摇杆）
- **没有飞智私有扩展**（无5A A5魔数、无原始数据模式）

### 蓝牙模式的局限性

| 特性 | PC模式 (2.4G/有线) | 蓝牙模式 |
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

**连接方式**：BLE蓝牙（与iOS/Switch相同协议）

- 标准BLE HID输入，映射到Android Gamepad API
- 无特殊Android专属协议 — 手柄就是一个通用BLE游戏手柄
- 飞智空间站APP可通过自定义BLE服务进行配置同步

**2.4G接收器在Android上的问题**：
- 接收器使用Vendor HID类，Android原生不识别
- 需要root用户态驱动（如[vader5-pro-android](https://github.com/LARo-developer/vader5-pro-android)项目）
- 原理：读取`/dev/hidraw`的私有32字节报告，通过`/dev/uinput`重新映射为Xbox Elite手柄

### iOS

**连接方式**：BLE蓝牙

- 标准BLE HID，不需要MFi认证
- iOS的Game Controller框架原生支持标准BLE HID游戏手柄
- 基本按键/摇杆/扳机功能可直接使用
- **注意**：飞智自定义服务（力反馈扳机、RGB灯效等）在iOS上未验证

### Switch (Nintendo Switch)

**连接方式**：BLE蓝牙

- 飞智手柄**不模拟Switch Pro手柄**（不像一些第三方手柄那样）
- 使用标准BLE HID协议连接
- Switch系统对第三方BLE HID手柄的支持有限
- **注意**：Switch模式的兼容性需要实际测试验证，部分手柄可能需要Switch端的蓝牙适配器

---

## 五、模式切换总结

### 硬件切换（物理开关）

```
背面拨动开关：
左侧 = PC模式 (2.4G接收器/USB有线)
中间 = 蓝牙模式 (BLE HID)
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

---

## 六、命令编号对照表

| 功能 | XInput模式 (2.4G) | DInput模式 (USB有线) |
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

### BLE HID报告（蓝牙）
- 标准HID报告描述符
- 按键/摇杆映射与XInput一致
- **无飞智私有扩展**
- **无IMU数据**（除非自定义服务提供）
- **不支持自适应扳机**

---

## 八、对自制接收器的意义

1. **PC模式是主要逆向目标**：私有NewXInput协议，需要完整实现
2. **蓝牙模式相对简单**：标准BLE HID，但缺少力反馈等高级功能
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
