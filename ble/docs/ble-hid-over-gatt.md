# BLE HID over GATT 协议详解

## 一、基础概念

- **BLE** = Bluetooth Low Energy（低功耗蓝牙）
- **HID** = Human Interface Device（人机接口设备）
- **GATT** = Generic Attribute Profile（通用属性协议）

BLE HID over GATT 就是：**通过蓝牙低功耗的 GATT 协议来传输 HID 数据**（键盘、鼠标、手柄等输入设备的数据）。

---

## 二、GATT 是什么

GATT 是 BLE 的数据传输框架，基于"属性"（Attribute）的概念：

```
一个 BLE 设备 = 一组"服务"(Service)
一个服务 = 一组"特征值"(Characteristic)
一个特征值 = 实际的数据 + 操作权限
```

类比理解：

```
整个设备 = 一栋办公楼
服务 (Service) = 楼层
特征值 (Characteristic) = 房间
数据 = 房间里的东西
```

每个特征值有唯一 UUID 标识，例如：

```
0x180F = 电池服务
0x180A = 设备信息服务
0x1812 = HID 服务（手柄用的就是这个）
```

---

## 三、HID 服务的结构

手柄作为 HID 设备，GATT 里至少有这些服务：

```
┌─────────────────────────────────────┐
│  GAP Service (0x1800)               │
│  ├── Device Name                    │
│  └── Appearance                     │
├─────────────────────────────────────┤
│  GATT Service (0x1801)              │
├─────────────────────────────────────┤
│  HID Service (0x1812) ★ 核心       │
│  ├── HID Information                │
│  ├── Report Map         ★ 报告描述 │
│  ├── Report (Input)     ★ 实际数据 │
│  ├── Report (Output)    ★ 震动等   │
│  ├── Protocol Mode                 │
│  └── HID Control Point             │
├─────────────────────────────────────┤
│  Battery Service (0x180F)           │
│  └── Battery Level                  │
├─────────────────────────────────────┤
│  飞智自定义服务 ★ 扩展功能         │
│  ├── 力反馈扳机控制                 │
│  ├── 宏/配置同步                    │
│  └── ...                            │
└─────────────────────────────────────┘
```

---

## 四、Report Map（报告描述符）

这是最关键的部分。它定义了手柄数据的**格式**，类似 USB HID 的报告描述符。

一个典型的 Xbox 手柄输入报告长这样：

```
Report ID: 0x01
┌──────────────────────────────────────────┐
│ 按钮 (Buttons)          │ 2 bytes       │
│   A, B, X, Y, LB, RB,   │               │
│   Back, Start, Guide,    │               │
│   L3, R3, D-pad Up/Down │               │
├──────────────────────────────────────────┤
│ 左摇杆 X               │ 2 bytes (int16)│
│ 左摇杆 Y               │ 2 bytes (int16)│
│ 右摇杆 X               │ 2 bytes (int16)│
│ 右摇杆 Y               │ 2 bytes (int16)│
├──────────────────────────────────────────┤
│ 左扳机 (LT)            │ 1 byte (0-255) │
│ 右扳机 (RT)            │ 1 byte (0-255) │
└──────────────────────────────────────────┘
```

Report Map 用 HID 描述符语言定义，看起来像这样：

```c
0x05, 0x01,        // Usage Page (Generic Desktop)
0x09, 0x05,        // Usage (Game Pad)
0xA1, 0x01,        // Collection (Application)
0x85, 0x01,        //   Report ID (1)

// 按钮
0x05, 0x09,        //   Usage Page (Button)
0x19, 0x01,        //   Usage Minimum (1)
0x29, 0x10,        //   Usage Maximum (16)
0x15, 0x00,        //   Logical Minimum (0)
0x25, 0x01,        //   Logical Maximum (1)
0x75, 0x01,        //   Report Size (1)
0x95, 0x10,        //   Report Count (16)
0x81, 0x02,        //   Input (Data, Var, Abs)

// 摇杆
0x05, 0x01,        //   Usage Page (Generic Desktop)
0x09, 0x30,        //   Usage (X)
0x09, 0x31,        //   Usage (Y)
0x16, 0x00, 0x80,  //   Logical Minimum (-32768)
0x26, 0xFF, 0x7F,  //   Logical Maximum (32767)
0x75, 0x10,        //   Report Size (16)
0x95, 0x02,        //   Report Count (2)
0x81, 0x02,        //   Input (Data, Var, Abs)

0xC0               // End Collection
```

---

## 五、数据通信流程

```
手柄 (Peripheral)              手机/电脑 (Central)
     │                                │
     │◄──── 连接请求 ────────────────│
     │──── 连接确认 ────────────────►│
     │                                │
     │◄──── 发现服务 ────────────────│
     │──── 返回服务列表 ────────────►│
     │                                │
     │◄──── 读取 Report Map ─────────│
     │──── 返回报告描述符 ──────────►│
     │                                │
     │◄──── 订阅通知 (Subscribe) ───│
     │     (CCCD = 0x0001)           │
     │                                │
     │════ 输入报告 (通知) ═════════►│
     │     每次按键/摇杆变化时发送    │
     │                                │
     │◄════ 输出报告 (震动) ════════│
     │     电脑发送震动指令           │
     │                                │
```

---

## 六、与传统蓝牙 HID 的区别

| 特性 | 传统蓝牙 HID | BLE HID over GATT |
|------|-------------|-------------------|
| 功耗 | 高 | 极低 |
| 延迟 | 10-30ms | 3-10ms |
| 数据量 | 大 | 小（每包最多 512 字节） |
| 连接速度 | 慢（2-3秒） | 快（<1秒） |
| 配对 | 需要 PIN 码 | 通常不需要 |
| 多设备 | 有限 | 支持更多 |

---

## 七、飞智八爪鱼5 的特点

飞智在标准 HID 协议基础上加了扩展：

```
标准部分（兼容 Xbox/PC）:
├── HID Service (0x1812)
├── 标准 Report Map
├── 按钮、摇杆、扳机数据
└── 震动输出

飞智自定义部分（扩展功能）:
├── 自定义 Service (UUID 私有)
├── 力反馈扳机控制
├── RGB 灯效控制
├── 宏录制/回放
├── 配置同步（通过飞智空间站 APP）
└── 陀螺仪数据（部分型号）
```

这就是为什么飞智手柄需要专用驱动或 APP 才能使用全部功能。

---

## 八、用 nRF52840 嗅探 BLE HID

```bash
# 步骤 1：安装 nRF Connect for Desktop
# 步骤 2：打开 nRF Sniffer for BLE
# 步骤 3：刷固件到 nRF52840 Dongle
# 步骤 4：打开 Wireshark

# 在 Wireshark 中你会看到：
# 1. 广播包 (ADV_IND) - 手柄在寻找连接
# 2. 连接事件 (CONNECT_IND) - 建立连接
# 3. 服务发现 (ATT Find Information) - 枚举服务
# 4. 读取 Report Map - 获取报告格式
# 5. 订阅通知 (Write CCCD) - 开始接收数据
# 6. 输入报告 (ATT Handle Value Notification) - 实际按键数据
```

---

## 九、关键 ATT 操作

GATT 基于 ATT（Attribute Protocol），主要有这些操作：

| 操作 | 方向 | 说明 |
|------|------|------|
| Read Request | Central → Peripheral | 读取特征值 |
| Write Request | Central → Peripheral | 写入特征值（需确认） |
| Write Command | Central → Peripheral | 写入特征值（不需确认） |
| Notification | Peripheral → Central | 主动推送数据 |
| Indication | Peripheral → Central | 主动推送数据（需确认） |

手柄输入报告通常使用 **Notification**，因为：
- 手柄需要主动推送按键变化
- 不需要每次都等主机请求
- 延迟更低

---

## 十、CCCD（Client Characteristic Configuration Descriptor）

CCCD 是一个特殊的描述符，用于控制通知/指示的开关：

```
写入 0x0001 = 启用 Notification
写入 0x0002 = 启用 Indication
写入 0x0000 = 禁用
```

这就是为什么主机在收到 Report Map 后，需要向 Report 特征值的 CCCD 写入 0x0001，手柄才会开始发送输入报告。
