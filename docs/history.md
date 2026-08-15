# 项目历史（尝试记录）

本文档记录项目的技术演进与尝试过程，保留历史脉络。代码与文档中与 nRF52840 相关的内容已于 2026-08 清理，仅在此留下记录。

## 阶段 1：nRF52840 BLE 尝试（搁置）

最初假设手柄使用 BLE HID over GATT，计划用 nRF52840 Dongle 做 BLE 接收器。

- 完成 M0-M3 输出管道基础设施：USB CDC ACM 双串口（Debug + Data）、文本/二进制格式化器（含单元测试）、UART TTL 输出后端
- 后经实测确认手柄蓝牙模式实际使用 **BR/EDR（经典蓝牙）**，而非 BLE，BLE 方向搁置

## 阶段 2：nRF52840 2.4GHz 无线尝试（失败）

假设手柄 2.4GHz 模式使用 Nordic ESB 或类似私有协议，尝试用 nRF52840 radio 空中抓包。

- 经 FCC 认证文件（FCC ID: 2AORE-K5）与内部芯片识别确认：手柄 2.4GHz 使用**星闪 SLE 1.0（NearLink）**
- SLE 的 PHY 层（Polar 码编码、中心调度、特定帧格式）与 Nordic radio 不兼容
- nRF52840 radio 只能解调 Nordic 兼容的 GFSK 信号，无法接收 SLE 信号，此路不通

详细分析见 `docs/sle-analysis.md` 第二节。

## 阶段 3：BS21 星闪开发（当前）

确认手柄 SLE 芯片为 P352903N1（星闪 SLE 1.0，飞智定制编号），采用海思 Hi2821（BS21E）芯片方案。

- 采购 Ai-BS21-32S-Kit（2 块，基于 Hi2821/BS21E）
- 开发环境选定海思官方 fbb_bs2x SDK（GitCode，Apache-2.0）
- 编译打包纯 Python（`python3 build.py standard-bs21e-1100e`），Linux 原生
- 烧录基于社区 ws63flash 适配（官方 BurnTool 仅 Windows）
- **烧录链路已打通（2026-08-15 实测）**：ws63flash 无需改代码即可烧录 BS21，
  握手（`0xf0`）+ ymodem 传输协议与 WS63 兼容；唯高波特率 921600 在 CH340 串口
  上不稳定，实测 **460800 稳定**。已成功完整烧录安信可 `init_sdk_fw.fwpkg`
  （6 镜像全 100%，含 loaderboot/partition/flashboot A+B/application/nv）。

详见 `docs/bs21-development.md`。

## nRF52840 成果保留说明

nRF52840 阶段（2025 初）的代码虽已清理，但其设计成果被 BS21 项目继承：

| 成果 | 去向 |
|------|------|
| `controller_state.h` 数据结构定义 | 迁移至 `wireless/bs21/src/controller_state.h`（去掉 Zephyr 依赖） |
| formatter 文本/二进制格式化设计思路 | BS21 侧重新实现（见实施计划 P3） |
| USB CDC 输出管道设计 | BS21 USB 2.0 CDC 重新实现 |
| 串口接口字符串识别方案（sysfs） | 后续 BS21 USB 输出阶段复用 |
