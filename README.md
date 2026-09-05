# 飞智八爪鱼5 自制接收器项目

## 项目目标

逆向工程飞智八爪鱼5 (Flydigi Apex 5) 的无线协议，实现自制接收器。

## 硬件环境

| 设备 | 型号 | 用途 |
|------|------|------|
| 手柄 | 飞智八爪鱼5 (Flydigi Apex 5) | 被逆向的目标设备 |
| H3863 开发板 | BearPi-Pico H3863 | **星闪 SLE 接收器开发（主平台）** |
| ESP32-WROOM-32E | DevKitC | 蓝牙方向（BR/EDR HID）研究（与 SLE 独立） |
| 电脑 | Arch Linux | 开发和调试 |

## 关键发现

手柄 2.4GHz 无线模式使用**星闪 SLE 1.0 (NearLink)**，非 Nordic ESB：

- FCC ID: 2AORE-K5，内部 SLE 芯片 P352903N1
- WS63 (H3863) 芯片原生支持 SLE 1.0 + Wi-Fi 6，适合做接收器

## 项目结构

```
flydigi-receiver/
├── docs/                          # 通用文档
│   ├── sle-analysis.md            # SLE 协议分析与可行性评估
│   ├── controller-modes.md        # 手柄模式与协议详解
│   ├── history.md                 # 项目历史与尝试记录
│   ├── sle-chip-comparison.md     # 芯片规格对比
│   ├── reference/                 # 官方文档存档
│   └── superpowers/               # 历史计划/设计（按日期）
├── tools/                         # 跨平台共享工具
│   ├── notify.sh                  # 提示音
│   └── capture_uart.py            # 串口抓取（SLE + BT 共享）
├── wireless/                      # 无线接收器（SLE 专用）
│   ├── ai-bs21-32s-kit/           # BS21 平台（已挂起）
│   ├── bearpi-pico-h3863/         # H3863 平台（主平台）
│   └── tools/
│       └── burn.py                # SLE 烧录（ws63flash）
├── bluetooth/                     # 蓝牙方向（与 wireless/ 平级）
│   └── esp32-wroom-32e/
│       ├── README.md              # 平台概览
│       ├── docs/development.md    # ESP-IDF 流程
│       ├── apps/                  # 各 app（hello_world 等）
│       │   └── hello_world/
│       │       ├── CMakeLists.txt
│       │       ├── main/
│       │       │   ├── CMakeLists.txt
│       │       │   └── main.c
│       │       ├── sdkconfig.defaults
│       │       └── README.md
│       ├── components/            # 跨 app 共享的 ESP-IDF 组件（自动发现）
│       ├── build/                 # 编译产物（不入库）
│       └── tools/                 # build.py / burn.py
├── .env                           # 顶层串口 + ctrl pin（不入库）
└── AGENTS.md                      # 工程记忆
```

## 开发状态

- **H3863（主平台）**：开发环境打通（Hello World 验证），SLE 接收器功能待迁移
- **BS21（已挂起）**：因 SDK 限制后期可能废弃
- **ESP32-WROOM-32E（蓝牙方向）**：M9 环境搭建完成，hello_world 编译/烧录/串口验证通过；M10+ 起做 BT inquiry、HID 主机连接

## 快速上手

```bash
# 构建 H3863（在平台目录下）
cd wireless/bearpi-pico-h3863
python tools/build.py              # default app
python tools/build.py --app sle_decoy

# 烧录（共享工具）
python3 wireless/tools/burn.py board_a <fwpkg>

# 抓 log
python3 wireless/tools/capture_uart.py --board-a --duration 60 --odir /tmp --ts
```

串口/复位配置见项目根 `.env`（不入库，模板见 `.env.example`）。

## 详细文档

- `AGENTS.md` — 工程记忆与开发规范
- `wireless/bearpi-pico-h3863/README.md` — H3863 平台指南
- `wireless/bearpi-pico-h3863/docs/design.md` — H3863 开发环境设计
- `wireless/ai-bs21-32s-kit/README.md` — BS21 平台归档

## 相关开源项目

- [flydigi-vader5](https://github.com/BANANASJIM/flydigi-vader5) - USB 协议文档
- [Flydigi5Pico](https://github.com/ruomox/Flydigi5Pico) - RP2350 USB 桥接
- [openflydigi](https://github.com/mkaliaha/openflydigi) - HID 命令协议逆向
- [Ai-BS21_SDK](https://gitee.com/Ai-Thinker-Open/Ai-BS21_SDK) - 安信可星闪 SDK
- [ws63flash](https://github.com/goodspeed34/ws63flash) - Linux 烧录工具
- [communication_nearlink_service](https://github.com/openharmony/communication_nearlink_service) - OpenHarmony 星闪协议栈
