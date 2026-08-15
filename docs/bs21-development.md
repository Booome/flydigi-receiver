# BS21 星闪开发文档

## 一、硬件

### 1.1 开发板：Ai-BS21-32S-Kit

已到货。

| 参数 | 规格 |
|------|------|
| 芯片 | Hi2821 (BS21) |
| CPU | RISC-V 32bit, 64MHz |
| SLE | SLE 1.0, 1M/2M/4M, 12Mbps |
| BLE | 5.4 |
| USB 2.0 | FS/HS, 480Mbps |
| RAM | 160KB SRAM |
| Flash | 1MB |
| 封装 | DIP-42, 69x25mm |
| 采购数量 | 2 块（SLE 通信需双设备） |

### 1.2 双 USB Type-C 接口

| 接口 | 连接方式 | 用途 |
|------|---------|------|
| USB1 | BS21 原生 USB 2.0 D+/D- | USB HID/CDC 设备模拟 |
| USB2 | CH340 USB转UART | 烧录/调试串口 |

开发时可以同时：USB2 接电脑烧录调试，USB1 做 USB HID 设备输出。

### 1.3 其他外设

- RGB LED (IO11红 / IO12绿 / IO13蓝)
- Power 按钮 + RST 复位按钮
- SWD 调试接口 (S_CLK / S_DAT)
- 29 个 GPIO
- 3x UART, 3x SPI, 2x I2C, 6x PWM, 8x ADC
- NFC Tag, PDM, I2S, QDEC, KeyScan

## 二、SDK 与开发环境

### 2.0 型号对应：BS21 = BS21E

安信可 Ai-BS21-32S-Kit 的芯片编号为 **Hi2821**，安信可称之为 "BS21"。
海思官方型号名为 **BS21E**（fbb_bs2x 规格表中的 BS21E）。

| | Hi2821 (安信可 "BS21") | 海思 BS21E |
|------|------|------|
| RAM | 160KB | 160KB |
| Flash | 1MB | 1MB |
| SLE | 1.0, 12Mbps | 1.0, 12Mbps |
| 空口速率 | 12Mbps | 12Mbps |

规格完全一致，为同一颗芯片。fbb_bs2x 的编译目标名是 `standard-bs21e-1100e`。

### 2.1 SDK 选项（2026-08 调研）

| 方案 | Stars | 最后更新 | License | 结论 |
|------|-------|---------|---------|------|
| **HiSilicon fbb_bs2x** (推荐) | 44 | 2026-08（活跃） | Apache-2.0 | 海思官方，最活跃，已迁 GitCode |
| XFusion | 27 | 2025-05（停更） | Apache-2.0 | 社区工具，`ports/pt` submodule 损坏 |
| Ai-BS21_SDK (安信可) | 12 | 2024-11（停更） | 无 license | 停更，无 license 有法律风险 |
| Docker (lualiliu/bs21_sdk) | 0 | 2024-12（停更） | 无 license | 几乎无人用 |

**结论：选用海思官方 fbb_bs2x**（Apache-2.0、最活跃、完整文档、免费样片申请）。
编译和打包（fwpkg）均为纯 Python，Linux 原生可用；烧录基于社区 `ws63flash` 适配（见 2.2.3）。

### 2.2 fbb_bs2x 环境搭建（Linux）

fbb_bs2x 官方文档只提供 Windows（HiSparkStudio）和 WSL 方案，但**编译打包是纯 Python，Linux 原生直接可用**；唯一 Windows 专属的是烧录工具 BurnTool，社区已破解（见 2.2.3）。

#### 2.2.1 获取 SDK

官方仓库已从 Gitee 迁移到 **GitCode**：

```bash
git clone https://gitcode.com/HiSpark/fbb_bs2x.git
```

> Gitee 上的 `HiSpark/fbb_bs2x` 仍有镜像同步，但 README 明确标注新地址在 GitCode。

#### 2.2.2 编译（Linux 原生）

```bash
cd fbb_bs2x/src
python3 build.py standard-bs21e-1100e     # 增量编译
python3 build.py -c standard-bs21e-1100e  # 全量编译
```

- 依赖：Python 3.8+、cmake、RISC-V 工具链（SDK 自带 `src/tools/bin/compiler`）
- 产出：`src/output/bs21e/fwpkg/standard-bs21e-1100e_all_in_one.fwpkg`
- 可选 `menuconfig`：`python3 build.py -c standard-bs21e-1100e menuconfig`

#### 2.2.3 烧录（Linux，基于社区 ws63flash）

官方 BurnTool 仅 Windows（支持命令行 `BurnTool.exe -com:N -bin:xxx.fwpkg -signalbaud:921600`）。
社区项目 [goodspeed34/ws63flash](https://github.com/goodspeed34/ws63flash)（GPLv3）逆向
BurnTool 实现了 Linux 原生烧录：

```bash
# 安装（Linux）
git clone https://github.com/goodspeed34/ws63flash.git
cd ws63flash && autoreconf -fi && ./configure && make && sudo make install

# 烧录（CH340 串口，Linux 原生 ch341 驱动）
# 实测 921600 在 CH340 串口上不稳定，460800 稳定（官方 baudList 亦含 460800）
ws63flash --flash /dev/ttyUSB0 /path/to/xxx.fwpkg -b460800
```

**fwpkg 格式**（BS2X 与 WS63 一致，同为海思 FBB 框架，已公开）：

```
FWPKG_HEAD (12B):  magic=0xefbeaddf + crc16 + imageNum + totalSize
IMAGE_INFO (52B):  name[32] + offset + imageSize + burnAddr + burnSize + type
```

ws63flash 已实现 fwpkg 解析（`src/fwpkg.h`，magic `0xefbeaddf` 与 fbb_bs2x 的
`src/tools/pkg/packet.py` 完全一致）和 WS63 串口烧录协议（帧头 `EF BE AD DE` +
命令 `0xf0` 握手 / `0x5a` 设波特率 / `0xd2` 下载 / `0x87` 复位）。

**已实测确认**：BS21 与 WS63 烧录协议兼容——握手（`0xf0`）成功、loaderboot ymodem 传输正常。
唯高波特率（921600）在 CH340 串口上易超时，**实测 460800 稳定**（`bs21.json` baudList 亦含 460800）。
`hispark-rs/hisi-flash-algorithm`（probe-rs 方案）已标注 "BS2X planned"，社区在推进。

### 2.3 SDK 架构（fbb_bs2x）

| 目录 | 内容 |
|------|------|
| `src/application/` | 示例代码、demo |
| `src/protocol/` | SLE/BLE 协议栈（含 GLE） |
| `src/drivers/` | 驱动库 |
| `src/kernel/` | LiteOS 内核 |
| `src/tools/pkg/` | fwpkg 打包脚本（纯 Python，开源） |
| `src/build/` | 构建脚本 + RISC-V 工具链 |
| `docs/zh-CN/` | 官方文档（开发环境、BurnTool 指导等） |

SLE API 头文件开放，协议栈为预编译静态库（待 SDK 就绪后确认具体路径）。

### 2.4 SLE 角色配置

本项目需要配置为 **G 节点**（SLE Central）。fbb_bs2x 通过 menuconfig 配置：

```bash
python3 build.py -c standard-bs21e-1100e menuconfig
```

> 具体 SLE 角色配置项名称待 SDK 就绪后确认（Ai-BS21_SDK 中为
> `CONFIG_SUPPORT_SLE_CENTRAL=y`，对应协议栈库 `sle-central`，fbb_bs2x 可能一致）。

## 三、SLE API 快速参考

详细分析见 `docs/sle-analysis.md` 第四节。

### 3.1 扫描（G 节点发现 T 节点）

```c
sle_set_seek_param(&seek_param);  // 配置扫描参数
sle_start_seek();                 // 开始扫描
// 回调: seek_result_cb(sle_seek_result_info_t *result)
// result->addr  -> 手柄 SLE 地址
// result->rssi  -> 信号强度
// result->data  -> 广播数据
```

### 3.2 连接

```c
sle_connect_remote_device(&addr);  // 发起连接
// 回调: connect_state_changed_cb()
```

### 3.3 配对

```c
sle_pair_remote_device(&addr);     // 发起配对
// 回调: pair_complete_cb()
```

### 3.4 数据收发 (SSAP)

```c
// SSAP (SLE Service Access Point) 用于连接后的数据传输
// 具体 API 在 sle_ssap.h 中定义
```

## 四、开发路线图

### M5: SLE 扫描验证（物料到货后第一步）

**目标**：确认 BS21 能扫描到手柄的 SLE 广播

- [ ] fbb_bs2x 环境搭建（Linux 编译 + ws63flash 烧录）
- [ ] 两块 BS21 Kit 互相验证 SLE 通信（排除环境问题）
- [ ] BS21 配置为 G 节点，执行 SLE 扫描
- [ ] 手柄开机（PC模式），观察扫描结果
- [ ] 记录手柄 SLE 地址、广播数据、RSSI
- [ ] 输出方式：USB2 串口输出扫描日志

**成功标准**：扫描到手柄 SLE 广播，获取地址和广播数据

### M6: SLE 连接尝试

**目标**：尝试与手柄建立 SLE 连接

- [ ] 基于 M5 获取的地址，发起连接
- [ ] 观察连接状态变化
- [ ] 如果连接建立，尝试配对
- [ ] 记录连接/配对过程中各阶段的状态
- [ ] 如果配对被拒，分析拒绝原因（地址过滤？SMP 密钥？）

**成功标准**：连接建立（即使配对失败）

### M7: 数据收发与协议解析

**目标**：如果连接成功，解析手柄数据

- [ ] 尝试发送 NewXInput 初始化命令（参考 USB 协议文档）
- [ ] 观察手柄响应
- [ ] 对比 USB 端报告格式（20字节标准 / 32字节扩展 5A A5 EF）
- [ ] 映射到 controller_state 结构

**成功标准**：收到手柄输入数据并正确解码

### M8: USB HID 输出

**目标**：BS21 作为 USB HID 设备输出手柄数据

- [ ] 配置 BS21 USB 2.0 为 HID 设备
- [ ] USB1 Type-C 连接 PC，模拟 Xbox 手柄
- [ ] SLE 接收数据 -> USB HID 输出

**成功标准**：PC 识别为游戏手柄，输入数据正确

### 备选：如果 SLE 连接失败

不拆解原装 dongle、不做 SWD dump 固件。备选方向：

1. 手柄切换到蓝牙模式（BR/EDR），用 ESP32 等设备走经典蓝牙 HID 连接
2. USB 抓包原装 dongle（usbmon），获取应用层 NewXInput 协议（不含 SLE 无线层）
3. 重新评估手柄 SLE 广播模式与配对机制，尝试其他连接参数

## 五、参考资料

- [Ai-BS21-32S-Kit 规格书](https://aithinker-static.oss-cn-shenzhen.aliyuncs.com/docs/Specification/Ai-BS21-32S-Kit_V1.1.0_%20Specification_CN.pdf)
- [海思官方 SDK fbb_bs2x (GitCode)](https://gitcode.com/HiSpark/fbb_bs2x) — 已从 Gitee 迁移
- [海思官方 SDK fbb_bs2x (Gitee 镜像)](https://gitee.com/HiSpark/fbb_bs2x)
- [fbb_bs2x 在线文档](https://docs.hisilicon.com/repos/fbb_bs2x/zh-CN/master/)
- [ws63flash (Linux 烧录工具)](https://github.com/goodspeed34/ws63flash)
- [hisi-flash-algorithm (probe-rs 烧录)](https://github.com/hispark-rs/hisi-flash-algorithm)
- [Ai-BS21_SDK (安信可，已停更)](https://github.com/Ai-Thinker-Open/Ai-BS21_SDK)
- [XFusion (社区，已停更)](https://github.com/x-eks-fusion/xfusion)
- [安信可星闪模组文档](https://docs.ai-thinker.com/sle-bs21/)
- [NearLink ToolBox](https://nearlink.docs.haohanyh.ovh/)
- [海思星闪技术介绍](https://www.hisilicon.com/cn/techtalk/nearlink/introduction)
- [海思开发者平台](https://developers.hisilicon.com/)
