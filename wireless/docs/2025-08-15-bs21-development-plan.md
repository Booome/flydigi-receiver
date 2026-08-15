# BS21 星闪接收器开发路径

## 概述

基于 Ai-BS21-32S-Kit（2块）开发飞智八爪鱼5 星闪 SLE 无线接收器。
采用"先验证后构建"策略：先确认 BS21 能与手柄通信，再构建完整功能。

- **SDK**: XFusion（原生 Linux 支持）
- **输出**: 先 CDC 串口，后 USB HID 手柄
- **代码复用**: 复用 nRF52840 `controller_state.h` 数据结构，BS21 端重新实现 formatter
- **双板分工**: 一块做接收器（主路径），另一块做基础测试
- **主路径**: 用实际手柄开发，不拆卸原装 dongle

## 整体结构

```
验证层（先做，确认可行性）
├── P0: 环境搭建        → 产出：BS21 开发能力
└── P1: 手柄连通性验证   → 产出：知道能不能连上手柄

（P1 成功后进入构建层，失败则重新评估方向）

构建层（P1 确认可行后做）
├── P2: 协议解析       → 产出：理解 SLE 上的应用层协议
├── P3: CDC 数据输出   → 产出：串口输出手柄数据
└── P4: USB HID 输出   → 产出：PC 识别为游戏手柄
```

## P0: 环境搭建

> 成功标准：两块 BS21 板能通过 SLE 连接、配对、收发数据。

### P0.1 XFusion 环境

| 任务 | 内容 | 预计 |
|------|------|------|
| P0.1.1 | 安装 cmake、python、pip 等系统依赖 | 5min |
| P0.1.2 | 安装 pycparser==2.21 | 5min |
| P0.1.3 | 安装 XFusion 工具链 (`get_xf bs21`) | 10min |
| P0.1.4 | 下载 BS21 SDK (`xf target -d`) | 10min |

### P0.2 Hello World

| 任务 | 内容 | 预计 |
|------|------|------|
| P0.2.1 | 创建 hello 项目，配置 Kconfig（UART 输出） | 10min |
| P0.2.2 | 编译 (`xf build`) | 5min |
| P0.2.3 | 烧录到 BS21 板 (`xf flash`)，USB2 CH340 串口接 PC | 10min |
| P0.2.4 | 验证串口输出 "Hello World"，确认工具链完整 | 5min |

### P0.3 双板 SLE 互验

| 任务 | 内容 | 预计 |
|------|------|------|
| P0.3.1 | 板A 配置为 T 节点：`enable_sle()` + `sle_start_announce()` | 15min |
| P0.3.2 | 板B 配置为 G 节点：`enable_sle()` + `sle_start_seek()` | 15min |
| P0.3.3 | 板B 扫描到板A，记录地址 + RSSI + 广播数据 | 10min |
| P0.3.4 | 板B 连接板A (`sle_connect_remote_device`)，验证连接回调 | 15min |
| P0.3.5 | 板B 配对角A (`sle_pair_remote_device`)，验证配对回调 | 15min |
| P0.3.6 | SSAP 数据收发：板A 发 "ping"，板B 回 "pong" | 20min |

## P1: 手柄连通性验证

> 成功标准：BS21 与手柄建立 SLE 连接（配对成功是加分项，连接建立即算通过）。

### P1.1 SLE 扫描手柄

| 任务 | 内容 | 预计 |
|------|------|------|
| P1.1.1 | 基于 P0.3 的 G 节点代码，调整扫描参数：全信道扫描、延长扫描时长 | 10min |
| P1.1.2 | 手柄拨到 PC 模式（左侧），开机，观察 LED 状态 | 5min |
| P1.1.3 | 执行扫描，遍历 `seek_result_cb` 回调，串口输出所有发现的 SLE 设备 | 15min |
| P1.1.4 | 筛选目标：对比广播数据中的设备名/厂商信息，确认手柄的 SLE 地址 | 10min |
| P1.1.5 | 记录：手柄 SLE 地址、广播模式、RSSI、广播数据 hex dump | 15min |

### P1.2 连接手柄

| 任务 | 内容 | 预计 |
|------|------|------|
| P1.2.1 | 用 P1.1 获取的地址，调用 `sle_connect_remote_device()` | 10min |
| P1.2.2 | 观察 `connect_state_changed_cb` 回调，记录连接状态变化和错误码 | 15min |
| P1.2.3 | 如果连接成功，查询连接角色 (`sle_get_connect_role`) 和连接参数 | 10min |

### P1.3 配对手柄

| 任务 | 内容 | 预计 |
|------|------|------|
| P1.3.1 | 连接成功后，调用 `sle_pair_remote_device()` | 5min |
| P1.3.2 | 观察 `pair_complete_cb` 回调，记录配对结果/错误码 | 10min |
| P1.3.3 | 如果配对失败，尝试 `sle_set_nv_smp_keys()` 设置密钥重试 | 15min |

## P2: 协议解析

> 成功标准：能从 SLE 数据帧中正确解析出按键、摇杆、扳机值，与 USB 有线模式结果一致。
>
> **假设**：SLE 无线端应用层 payload 格式与 USB 端一致（5A A5 魔数、命令编号、报告格式）。此假设在 P2.1 中验证。

### P2.1 应用层初始化

| 任务 | 内容 | 预计 |
|------|------|------|
| P2.1.1 | 参考 USB 协议文档，构造 NewXInput 初始化序列：`5A A5 01 02 03`（设备信息）、`5A A5 A1 02 A3`（序列号）、`5A A5 02 02 04`（配置读取） | 15min |
| P2.1.2 | 通过 SSAP 发送初始化命令，串口打印每次发送的 hex | 10min |
| P2.1.3 | 观察手柄响应：串口打印每次收到的 hex，对比 USB 端预期响应 | 15min |

### P2.2 数据接收验证

| 任务 | 内容 | 预计 |
|------|------|------|
| P2.2.1 | 如果手柄有数据上报，记录原始 hex dump（至少 100 帧） | 10min |
| P2.2.2 | 对比 USB 端 20 字节标准报告格式，逐字节映射 | 15min |
| P2.2.3 | 构造 `ThirdPartyControl` 命令 (0x11)，开启第三方控制模式 | 15min |
| P2.2.4 | 对比 USB 端 32 字节扩展报告格式（5A A5 EF 魔数），逐字节映射 | 15min |

### P2.3 数据结构定义

| 任务 | 内容 | 预计 |
|------|------|------|
| P2.3.1 | 从 nRF52840 `wireless/` 复制 `controller_state.h` 到 BS21 项目 | 5min |
| P2.3.2 | 根据 P2.2 的映射结果，调整字段偏移（如果 SLE 端与 USB 端有差异） | 10min |
| P2.3.3 | 编写 `sle_parser.c`：原始字节 → `controller_state` 结构体 | 15min |

## P3: CDC 数据输出

> 成功标准：PC 端能通过串口持续获取手柄输入数据，文本和二进制两种格式均可解析。

### P3.1 USB CDC 配置

| 任务 | 内容 | 预计 |
|------|------|------|
| P3.1.1 | 查阅 BS21 SDK USB CDC 示例代码，了解 API 和配置方式 | 15min |
| P3.1.2 | 配置 Kconfig 启用 USB CDC（`CONFIG_USB_CDC_ACM=y` 等） | 10min |
| P3.1.3 | 编译烧录，验证 `/dev/ttyACM*` 出现，串口工具可连接 | 10min |

### P3.2 文本 Formatter

| 任务 | 内容 | 预计 |
|------|------|------|
| P3.2.1 | 参考 nRF52840 `formatter_text.c`，在 BS21 上重新实现文本格式化 | 20min |
| P3.2.2 | 格式化输出：每帧一行，包含时间戳、按键位图、摇杆坐标、扳机值 | 10min |
| P3.2.3 | 连接手柄，验证 CDC 输出与手柄操作一致 | 10min |

### P3.3 二进制 Formatter

| 任务 | 内容 | 预计 |
|------|------|------|
| P3.3.1 | 参考 nRF52840 `formatter_binary.c`，在 BS21 上重新实现二进制帧格式 | 15min |
| P3.3.2 | 定义帧格式：`[magic:2B][len:2B][seq:1B][controller_state:N][crc:2B]` | 10min |
| P3.3.3 | 复用 nRF52840 的 Python 二进制解析脚本，适配 BS21 帧格式 | 10min |
| P3.3.4 | 端到端验证：手柄 → BS21 SLE → CDC 二进制 → Python 脚本 → 人类可读输出 | 15min |

## P4: USB HID 输出

> 成功标准：PC 识别为 Xbox 手柄，游戏可正常使用，震动反馈可用。

### P4.1 USB HID 配置

| 任务 | 内容 | 预计 |
|------|------|------|
| P4.1.1 | 查阅 BS21 SDK USB HID 示例代码，了解 API | 15min |
| P4.1.2 | 配置 Kconfig 启用 USB HID（`CONFIG_USB_HID=y` 等） | 10min |
| P4.1.3 | 编写 Xbox 360 手柄 HID 报告描述符（按键 + 2 摇杆 + 2 扳机 + 十字键） | 20min |

### P4.2 数据映射

| 任务 | 内容 | 预计 |
|------|------|------|
| P4.2.1 | 编写 `hid_mapper.c`：`controller_state` → Xbox HID 报告 | 15min |
| P4.2.2 | 处理数据范围映射：摇杆 int16 → 16bit、扳机 uint8 → 16bit | 10min |
| P4.2.3 | 编译烧录，USB1 接 PC，验证设备管理器识别为 "Xbox 360 Controller" | 10min |

### P4.3 端到端验证

| 任务 | 内容 | 预计 |
|------|------|------|
| P4.3.1 | 使用 `jstest` / `evtest` 验证按键和摇杆输入 | 10min |
| P4.3.2 | 使用 Steam / 游戏验证手柄功能正常 | 10min |
| P4.3.3 | 测试震动命令：PC → HID Output Report → BS21 → SLE → 手柄 | 20min |

## 任务汇总

| 阶段 | 任务数 | 预计时间 |
|------|--------|---------|
| P0: 环境搭建 | 9 | ~2h |
| P1: 手柄连通性验证 | 8 | ~1.5h |
| P2: 协议解析 | 8 | ~2h |
| P3: CDC 数据输出 | 7 | ~1.5h |
| P4: USB HID 输出 | 7 | ~2h |
| **总计** | **39** | **~9h** |

## 关键决策点

- **P0 完成后**：双板 SLE 互验是否通过？不通过说明环境/工具链问题，需要排查。
- **P1 完成后**：手柄连接是否成功？不成功说明手柄 SLE 配置不兼容第三方连接，项目需要重新评估方向（不包含拆卸原装 dongle 或 SWD dump 方案）。

## 代码结构（目标）

```
wireless/bs21/
├── CMakeLists.txt
├── prj.conf              # Kconfig
├── src/
│   ├── main.c            # 主循环
│   ├── sle_manager.c/h   # SLE 扫描/连接/配对
│   ├── sle_parser.c/h    # 原始字节 → controller_state
│   ├── controller_state.h # 从 nRF52840 复用
│   ├── formatter_text.c/h  # 文本格式化
│   ├── formatter_binary.c/h # 二进制格式化
│   ├── hid_mapper.c/h    # controller_state → HID 报告
│   └── usb_cdc.c/h       # USB CDC 输出
├── scripts/
│   └── parse_binary.py   # PC 端二进制解析
└── tests/
    └── test_parser.c     # 单元测试
```

## 参考资料

- `docs/sle-analysis.md` - SLE 协议分析
- `docs/bs21-development.md` - BS21 开发板、SDK 与开发路线图
- `docs/controller-modes.md` - 手柄模式与协议详解
- nRF52840 `wireless/firmware/` - 现有实现参考