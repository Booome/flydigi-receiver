# Flydigi Receiver Project

## 项目状态

手柄使用**星闪 SLE 1.0 (NearLink)** 进行 2.4GHz 无线通信，基于
**Ai-BS21-32S-Kit**（BS21 开发板，2 块）开发 SLE 接收器。

历史尝试（nRF52840 BLE / 2.4GHz 无线）见 `docs/history.md`。

详细分析见：
- `docs/sle-analysis.md` - SLE 协议分析与逆向可行性评估
- `docs/bs21-development.md` - BS21 开发板、SDK 与开发路线图
- `docs/controller-modes.md` - 手柄模式与协议详解
- `docs/history.md` - 项目历史与技术演进记录

## 平台

### BS21 开发板（已到货）

Ai-BS21-32S-Kit，基于 Hi2821 (BS21，海思型号名 BS21E) 芯片：
- SLE 1.0 + BLE 5.4 + USB 2.0
- 双 Type-C：USB1 原生 USB 2.0（HID/CDC），USB2 CH340 串口（烧录/调试）
- SDK: 海思官方 **fbb_bs2x**（GitCode，Apache-2.0，最活跃）
- 编译打包纯 Python（`python3 build.py standard-bs21e-1100e`），Linux 原生
- 烧录基于社区 ws63flash 适配（官方 BurnTool 仅 Windows）
- 开发环境搭建和路线图见 `docs/bs21-development.md`

## 手柄硬件信息

- 型号：飞智八爪鱼5 (Flydigi Apex 5)
- FCC ID：2AORE-K5
- 2.4GHz 芯片：P352903N1（星闪 SLE 1.0，飞智定制编号）
- 蓝牙芯片：BP1Y303-D4（BR/EDR）
- USB VID/PID：0x37D7 / 0x2501
