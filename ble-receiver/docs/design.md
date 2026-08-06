# 固件 1：蓝牙接收器设计文档

## 一、概述

### 1.1 项目目标

基于 nRF52840 Dongle，实现一个 BLE 中央设备（Central），与飞智八爪鱼5
手柄建立蓝牙连接，解析手柄的 HID 输入报告，并将手柄状态通过串口
（USB CDC / UART TTL）或 USB HID 接口输出。

### 1.2 用途

- **串口模式**：将手柄输入转换为串口数据，驱动小车、机器人等嵌入式设备
- **USB 手柄模式**（学习目的）：被电脑识别为标准游戏手柄

### 1.3 技术可行性

八爪鱼5 蓝牙模式下是**标准 BLE HID over GATT 设备**，走公开协议，
不需要破解私有协议。nRF52840 支持 BLE Central 角色，Zephyr RTOS
提供完整的 BLE 协议栈和 HOGP（HID over GATT Profile）示例。

| 维度 | 结论 |
|------|------|
| 协议 | 标准 BLE HID over GATT (Service 0x1812) |
| nRF52840 角色 | BLE Central，主动连接手柄 |
| 数据获取 | 订阅 Report Input 的 Notification (CCCD=0x0001) |
| 报告格式 | 标准 Xbox 手柄布局，需抓包确认细节 |
| 框架 | Zephyr RTOS (NCS v3.4.0) |

### 1.4 已知限制

- 蓝牙模式**无飞智私有扩展**（无 5A A5 魔数，无 M1-M4/C/Z 等扩展按键）
- 蓝牙模式**不支持自适应扳机**
- IMU 数据在蓝牙模式下**未验证**（可能仅有自定义服务提供）
- 震动部分型号**不支持**

---

## 二、目录结构

```
ble-receiver/
  docs/
    design.md              # 本文档
  firmware/                # nRF52840 固件
    src/                   # 固件源码
    tests/
      unit/                # native_sim + ztest 单元测试
      scripts/             # PC 端验证脚本 (Python)
      integration/         # 硬件集成测试文档
    CMakeLists.txt          # Zephyr 构建配置
    prj.conf                # Zephyr 项目配置
  host/                    # 上位机测试工具（后续）
```

---

## 三、架构设计

### 3.1 分层架构（自底向上开发）

```
手柄 --BLE--> raw HID report
                 |
         +-------+--------+
         |                |
    HID Parser       USB HID 透传
    (仅串口模式)      (直接转发 raw report)
         |                |
  controller_state    USB HID Backend
         |           电脑识别为手柄
    Formatter
  (文本 / 二进制)
         |
    +----+----+
    |         |
  USB CDC   UART TTL
```

- **格式化层只服务于串口模式**（CDC + TTL）
- **USB HID 模式直接透传 raw report**，不经过解析和格式化
- 输出后端通过 **Kconfig 编译时选择**，生成不同固件版本

### 3.2 组件职责

| 组件 | 输入 | 输出 | 可独立测试 |
|------|------|------|-----------|
| BLE Central | 手柄广播 / HID 通知 | raw HID report bytes | 用手机 nRF Connect 模拟手柄 |
| HID Parser | raw report + report map | `controller_state` | PC 端 ztest 单元测试 |
| Formatter | `controller_state` | 文本行 / 二进制帧 | PC 端 ztest 单元测试 |
| Output Backend | 格式化后的 bytes | USB CDC / UART / HID | 硬件集成测试 |

关键设计点：**HID Parser 和 Formatter 是纯逻辑**，不依赖硬件，
可以在 PC 上用 Zephyr `native_sim` 目标跑 `ztest` 自动化单元测试。

---

## 四、核心数据结构

```c
#include <stdint.h>

// Button bitmask definitions
#define BTN_A       BIT(0)
#define BTN_B       BIT(1)
#define BTN_X       BIT(2)
#define BTN_Y       BIT(3)
#define BTN_LB      BIT(4)
#define BTN_RB      BIT(5)
#define BTN_BACK    BIT(6)
#define BTN_START   BIT(7)
#define BTN_GUIDE   BIT(8)
#define BTN_L3      BIT(9)
#define BTN_R3      BIT(10)
#define BTN_DUP     BIT(11)
#define BTN_DDOWN   BIT(12)
#define BTN_DLEFT   BIT(13)
#define BTN_DRIGHT  BIT(14)
// bit 15 reserved

// Structured controller state - standard interface between layers
struct controller_state {
    uint16_t buttons;      // Button bitmask (see BTN_* above)
    uint8_t  lt;           // Left trigger  0-255
    uint8_t  rt;           // Right trigger  0-255
    int16_t  lx;           // Left stick X  -32768 ~ 32767
    int16_t  ly;           // Left stick Y
    int16_t  rx;           // Right stick X
    int16_t  ry;           // Right stick Y
    uint8_t  battery;      // Battery level 0-100
};
```

---

## 五、数据格式定义

### 5.1 文本格式（串口模式）

```
LX:%06d,LY:%06d,RX:%06d,RY:%06d,BTN:%04x,LT:%03d,RT:%03d\r\n
```

示例输出：
```
LX:01234,LY:-05678,RX:00000,RY:32767,BTN:000a,LT:128,RT:000\r\n
```

| 字段 | 格式 | 范围 | 说明 |
|------|------|------|------|
| LX/LY/RX/RY | `%06d` | -32768 ~ 32767 | 含符号，固定6字符 |
| BTN | `%04x` | 0000 ~ ffff | 按钮位掩码，4位hex小写 |
| LT/RT | `%03d` | 000 ~ 255 | 扳机，3位十进制 |
| 行尾 | `\r\n` | - | CRLF |

### 5.2 二进制帧格式（串口模式）

```
偏移  大小  字段        类型        说明
0     1    frame_head1  uint8      0xAA
1     1    frame_head2  uint8      0x55
2     1    length       uint8      有效数据长度（固定13）
3-4   2    buttons      uint16 LE  按钮位掩码
5-6   2    lx           int16 LE   左摇杆X
7-8   2    ly           int16 LE   左摇杆Y
9-10  2    rx           int16 LE   右摇杆X
11-12 2    ry           int16 LE   右摇杆Y
13    1    lt           uint8      左扳机
14    1    rt           uint8      右扳机
15    1    checksum     uint8      字节[2..14]累加和 & 0xFF
```

总帧长：**16 字节**（固定）

### 5.3 文本/二进制切换

通过编译时宏 `CONFIG_OUTPUT_FORMAT_TEXT` /
`CONFIG_OUTPUT_FORMAT_BINARY` 选择，空间允许时后续可改为运行时切换。

### 5.4 按钮位掩码定义

```
bit 0  = A
bit 1  = B
bit 2  = X
bit 3  = Y
bit 4  = LB (Left Bumper)
bit 5  = RB (Right Bumper)
bit 6  = Back
bit 7  = Start
bit 8  = Guide (Home)
bit 9  = L3 (Left Stick Press)
bit 10 = R3 (Right Stick Press)
bit 11 = D-Pad Up
bit 12 = D-Pad Down
bit 13 = D-Pad Left
bit 14 = D-Pad Right
bit 15 = 保留
```

---

## 六、开发里程碑（TODO List）

每个里程碑都有明确的可测试标准。逐条 pick 执行，完成一条勾选一条。

### M0：环境搭建 + Hello World ✅

**目标**：验证工具链可用，建立烧录和调试通道

**任务**：
- [x] 配置 nrfutil 到 PATH（符号链接到 `~/.local/bin/`）
- [x] 安装 nrfutil 子命令：device, sdk-manager, nrf5sdk-tools
- [x] 编译 Zephyr `blinky` sample 烧录到 Dongle
- [x] 编译 Zephyr `CDC ACM` sample 烧录到 Dongle
- [x] 编写编译/烧录指南 (`docs/build-and-flash.md`) 和项目 Makefile
- [x] 创建 AGENTS.md 工程记忆

**测试标准**：
- ✅ Dongle LED 闪烁（blinky 验证）
- ✅ 电脑 `ls /dev/ttyACM*` 出现串口设备（CDC 验证）
- ✅ 串口收到 Zephyr 启动 banner 输出

---

### M1：USB CDC 后端 + 文本格式化 + 模拟数据

**目标**：验证输出层和文本格式化，不依赖手柄

**任务**：
- [x] 实现 `controller_state` 数据结构 (`src/controller_state.h`)
- [x] 实现文本格式化器 (`src/formatter_text.c`)
- [x] 实现 USB CDC output backend (`src/output_cdc.c`)
- [x] 实现统一输出接口 `output_send(buf, len)` (`src/output.h`)
- [x] 用定时器生成模拟 `controller_state` 数据 (`src/main.c`)
- [x] 编写文本格式化单元测试 (`tests/unit/test_formatter_text.c`)

**测试标准**：
- **硬件**：电脑串口工具收到文本行
  `LX:00000,LY:00000,RX:00000,RY:00000,BTN:0000,LT:000,RT:000\r\n`
  模拟数据定时变化
- **单元测试 (native_sim)**：
  - 全零状态输出正确文本
  - 摇杆极值（-32768 / 0 / 32767）输出正确
  - 扳机极值（0 / 128 / 255）输出正确
  - 按钮全按下 `BTN:ffff`
  - 单按钮逐个测试位掩码正确
  - 格式稳定（同一输入多次调用结果一致）
  - 行尾 `\r\n` 存在
  - 缓冲区不溢出

---

### M2：二进制格式化

**目标**：验证二进制帧格式

**任务**：
- [x] 实现二进制格式化器 (`src/formatter_binary.c`)
- [x] 添加 `CONFIG_OUTPUT_FORMAT_TEXT/BINARY` Kconfig 切换
- [x] 编写二进制格式化单元测试 (`tests/unit/test_formatter_binary.c`)
- [x] 编写 PC 端解析验证脚本 (`tests/scripts/parse_binary.py`)

**测试标准**：
- **硬件**：串口收到二进制帧数据
- **单元测试 (native_sim)**：
  - 帧头 `0xAA 0x55` 正确
  - 长度字段 == 13
  - buttons uint16 小端序
  - 摇杆 int16 小端序
  - 扳机 uint8
  - 校验和正确（全零 / 极值 / 随机值）
  - 帧总长度 == 16 字节
- **Python 脚本**：接收串口二进制帧，解析并校验通过

---

### M3：UART TTL 后端

**目标**：验证物理 UART 输出

**任务**：
- [x] 实现 UART output backend (`src/output_uart.c`)
- [x] 添加 `CONFIG_OUTPUT_BACKEND_UART` Kconfig 选项
- [x] 确定波特率（默认 115200）

**测试标准**：
- **硬件**：USB-TTL 转换器接 Dongle GPIO TX 引脚（P0.20）
- 电脑收到与 USB CDC 相同格式的数据（文本或二进制）
- 波特率 115200 稳定无丢包

---

### M4：BLE 扫描

**目标**：验证 BLE Central 扫描功能

**任务**：
- [ ] 实现 BLE 扫描模块 (`src/ble_central.c`)
- [ ] 按设备名过滤（含 `Flydigi` 的广播名）
- [ ] 输出扫描结果到日志（RTT 或 CDC）

**测试标准**：
- **硬件**：手柄进入蓝牙配对模式（拨动开关到中间，蓝色 LED）
- Dongle 日志显示发现设备（设备名含 `Flydigi`，显示 MAC 地址）

---

### M5：BLE 连接 + HID 订阅

**目标**：验证 BLE 连接和 HID 通知接收

**任务**：
- [ ] 实现连接手柄（`ble_central.c` 扩展）
- [ ] 实现 GATT 服务发现（找 HID Service 0x1812）
- [ ] 读取 Report Map（保存报告描述符）
- [ ] 订阅 Report Input 的 Notification（CCCD 写 0x0001）
- [ ] 将收到的 raw HID report 通过日志 hex dump 输出

**测试标准**：
- **硬件**：日志显示连接成功
- 日志显示发现 HID Service (0x1812)
- 日志显示 Report Map 内容（hex dump）
- 操作手柄时，日志显示收到 Notification（hex dump）

**重要产出**：此阶段抓包到八爪鱼5 蓝牙模式的真实 Report Map 和
Input Report 格式，用于后续 M6 的 Parser 实现。

---

### M6：HID 报告解析

**目标**：将 raw HID report 解析为 `controller_state`

**任务**：
- [ ] 实现 HID Parser (`src/hid_parser.c`)
- [ ] 根据 M5 抓包的真实 Report Map 解析 Input Report
- [ ] 输出结构化状态到日志
- [ ] 编写 HID Parser 单元测试 (`tests/unit/test_hid_parser.c`)
  - 使用 M5 抓包的真实报告数据作为测试输入
- [ ] 如格式与设计文档假设不同，更新 Parser 和数据结构

**测试标准**：
- **硬件**：日志显示 `buttons=0x000a lt=128 rt=0 lx=1234 ly=-5678 ...`
- 操作手柄各按键 / 摇杆 / 扳机，日志值正确变化
- **单元测试 (native_sim)**：
  - 全零报告 -> 全零状态
  - 全按钮按下 -> buttons == 0x7FFF
  - 单按钮逐个测试（15 个按钮位）
  - 摇杆极值（-32768 / 0 / 32767）
  - 扳机极值（0 / 128 / 255）
  - 字节序验证（int16 小端解析正确）
  - 组合输入（按钮 + 摇杆 + 扳机全非零）
  - 空指针防御（NULL report 不崩溃）

---

### M7：端到端整合

**目标**：手柄真实数据通过串口输出

**任务**：
- [ ] 整合 BLE -> HID Parser -> Formatter -> Output 全链路
- [ ] 移除模拟数据，接入真实手柄数据
- [ ] 验证文本格式端到端
- [ ] 验证二进制格式端到端

**测试标准**：
- **硬件**：操作手柄，串口实时收到数据
- 摇杆移动，数据连续变化（无卡顿）
- 按键按下 / 释放即时反映
- 扳机渐变，值连续变化
- 文本格式和二进制格式都验证通过（用 Python 脚本）

---

### M8：断线重连

**目标**：验证健壮性

**任务**：
- [ ] 实现 BLE 断线检测
- [ ] 实现自动重连逻辑
- [ ] 断线时通过串口输出状态信息
- [ ] 重连后恢复数据输出

**测试标准**：
- **硬件**：关闭手柄电源，日志显示断线，串口输出 `STATUS:DISCONNECTED`
- 重新打开手柄，日志显示重连成功，串口恢复数据输出
- 手柄超出范围再回来，同样自动重连

---

### M9：USB HID 透传（可选）

**目标**：Dongle 被电脑识别为标准游戏手柄

**任务**：
- [ ] 根据 M5 抓包的 Report Map 设计 USB HID 描述符
- [ ] 实现 USB HID output backend (`src/output_hid.c`)
- [ ] 添加 `CONFIG_OUTPUT_BACKEND_HID` Kconfig 选项
- [ ] 实现 raw report 透传逻辑

**测试标准**：
- **硬件**：Dongle 插电脑，`lsusb` 显示 HID Gamepad 设备
- `jstest /dev/input/js0` 或游戏面板能识别按键 / 摇杆
- 按键 / 摇杆 / 扳机响应正常

---

## 七、测试策略

### 7.1 测试分层

| 层级 | 目标 | 工具 | 自动化 |
|------|------|------|--------|
| 单元测试 | 纯逻辑（Parser / Formatter） | Zephyr `native_sim` + `ztest` | 是 |
| 硬件集成测试 | 硬件相关（BLE / USB / UART） | 人工操作 + 日志观察 | 否 |
| PC 端验证 | 串口数据验证 | Python 脚本 | 半自动 |

### 7.2 测试与里程碑对应关系

| 里程碑 | 单元测试 | 硬件测试 | Python 脚本 |
|--------|---------|---------|------------|
| M0 | - | LED / 串口识别 | - |
| M1 | Formatter 文本全用例 | 串口收到文本行 | parse_text.py |
| M2 | Formatter 二进制全用例 | 串口收到二进制帧 | parse_binary.py |
| M3 | - | USB-TTL 收到数据 | - |
| M4 | - | 日志显示发现手柄 | - |
| M5 | - | 日志显示 HID hex dump | - |
| M6 | HID Parser 全用例 | 日志显示结构化状态 | - |
| M7 | - | 操作手柄实时输出 | parse_text/binary.py |
| M8 | - | 断线重连恢复 | - |
| M9 | - | 电脑识别为手柄 | - |

### 7.3 硬件集成测试文档格式

每个里程碑的硬件测试记录在 `firmware/tests/integration/test-manual.md` 中：

```
### Mx - 名称
**前置条件**: ...
**步骤**:
  1. ...
**预期结果**: ...
**验证方法**: ...
**实际结果**: ___（测试时填写）
```

### 7.4 重要说明

单元测试的 HID report 测试数据，在 M5 抓包前先基于**文档描述的标准
Xbox 手柄格式**编写。M5 抓包到八爪鱼5 的真实 Report Map 后，如果格式
有差异，更新 Parser 并补充真实数据的测试用例。这是合理的增量策略：
先建框架，后填真实数据。

---

## 八、风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| 八爪鱼5 蓝牙 Report Map 与标准格式不同 | Parser 需返工 | M5 先抓包确认格式，Parser 基于 5.4 节按钮位掩码设计，格式变化只改映射表 |
| 扩展按键（M1-M4/C/Z）蓝牙下不可用 | 功能缺失 | 文档已确认蓝牙模式无私有扩展；标准按键足够控制小车/机器人 |
| IMU 数据蓝牙下不可用 | 无陀螺仪/加速度 | 列为可选，M5 验证自定义服务是否提供 |
| 配对流程未知 | 连接失败 | Zephyr 支持多种配对模式，M4/M5 逐步排查 |
| 震动不支持 | 无力反馈 | 文档已标注，不影响输入功能 |
| BLE 连接不稳定 | 数据丢包 | M8 实现断线重连；调整连接参数（interval/timeout） |

