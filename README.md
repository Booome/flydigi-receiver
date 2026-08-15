# 飞智八爪鱼5 自制接收器项目

## 项目目标

逆向工程飞智八爪鱼5 (Flydigi Apex 5) 的无线协议，实现自制接收器。

## 硬件环境

| 设备 | 型号 | 用途 |
|------|------|------|
| 手柄 | 飞智八爪鱼5 (Flydigi Apex 5) | 被逆向的目标设备 |
| BS21 开发板 | Ai-BS21-32S-Kit ×2 | **星闪 SLE 接收器开发** |
| 电脑 | Arch Linux (KDE Plasma Wayland) | 开发和调试 |

## 关键发现

手柄 2.4GHz 无线模式使用**星闪 SLE 1.0 (NearLink)**，非 Nordic ESB：

- FCC ID: 2AORE-K5，内部 SLE 芯片 P352903N1
- BS21 芯片（Hi2821/BS21E）原生支持 SLE 1.0 + USB 2.0，适合做接收器

详细分析见 [SLE 协议分析](docs/sle-analysis.md)。历史尝试见 [项目历史](docs/history.md)。

## 项目结构

```
flydigi-receiver/
├── docs/           # 全局文档
│   ├── sle-analysis.md        # SLE 协议分析与可行性评估
│   ├── bs21-development.md    # BS21 开发板、SDK 与开发路线图
│   ├── controller-modes.md    # 手柄模式与协议详解
│   └── history.md             # 项目历史与尝试记录
├── wireless/       # 无线接收器
│   └── bs21/       # BS21 SLE 接收器代码
└── AGENTS.md
```

## 开发状态

星闪 SLE 接收器开发中，基于安信可 **Ai-BS21_SDK**（overlay 模式，SLE-only `bs21-n1100-rcu` target，512KB flash）。

- 开发路线图详见 [BS21 开发文档](docs/bs21-development.md)

## 相关开源项目

- [flydigi-vader5](https://github.com/BANANASJIM/flydigi-vader5) - USB 协议文档
- [Flydigi5Pico](https://github.com/ruomox/Flydigi5Pico) - RP2350 USB 桥接
- [openflydigi](https://github.com/mkaliaha/openflydigi) - HID 命令协议逆向
- [Ai-BS21_SDK](https://gitee.com/Ai-Thinker-Open/Ai-BS21_SDK) - 安信可星闪 SDK
- [ws63flash](https://github.com/goodspeed34/ws63flash) - Linux 烧录工具
