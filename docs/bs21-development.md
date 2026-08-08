# BS21 星闪开发文档

## 一、硬件

### 1.1 开发板：Ai-BS21-32S-Kit

已采购，等待到货。

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

### 2.1 SDK 选项

| 方案 | Linux 支持 | 说明 |
|------|-----------|------|
| **XFusion** (推荐) | 原生 | 社区构建工具，自动处理 SDK 下载和工具链 |
| Docker (lualiliu/bs21_sdk) | 原生 | Dev Container 封装完整环境 |
| Ai-BS21_SDK (官方) | 需适配 | CMake + Python + RISC-V GCC，官方推荐 Windows |
| HiSilicon fbb_bs2x (官方) | 需适配 | 海思官方 SDK，Gitee 上有 |

### 2.2 XFusion 环境搭建（Arch Linux）

```bash
# 安装依赖
sudo pacman -S cmake python python-pip

# 安装 XFusion (参考 https://www.coral-zone.cc/document/zh_CN/)
pip install pycparser==2.21
# 按照 XFusion 文档安装 xf 工具

# 激活 BS21 目标
get_xf bs21

# 下载 SDK
xf target -d

# 编译
xf build

# 烧录
xf flash
```

### 2.3 SDK 架构

| 目录 | 内容 | 开源状态 |
|------|------|---------|
| `application/` | 示例代码、demo | 开源 |
| `include/middleware/services/bts/sle/` | SLE API 头文件 (14个) | 开源 |
| `protocol/bt/host/gle/` | SLE 协议栈 | 闭源 (libbth_gle.a) |
| `drivers/` | 驱动库 | 闭源 |
| `kernel/` | LiteOS 内核 | 闭源 |

SLE API 头文件完整开放，协议栈为预编译静态库。

### 2.4 SLE 角色 Kconfig 配置

本项目需要配置为 **G 节点**（SLE Central）：

```
CONFIG_SUPPORT_SLE_CENTRAL=y
```

对应协议栈库: `sle-central` 或 `sle-central-release`

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

- [ ] XFusion 环境搭建 (Arch Linux)
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

1. 拆解原装 dongle，识别 SLE 芯片
2. 尝试 SWD/JTAG dump dongle 固件
3. 提取配对密钥和 SLE 地址
4. 用 BS21 伪装 dongle 身份连接手柄

## 五、与 nRF52840 项目的关系

### 5.1 nRF52840 wireless 固件（已完成 M0-M3）

nRF52840 的无线固件（USB CDC + UART + formatter）**不适用于 SLE 通信**，
但其架构设计可以作为 BS21 项目的参考：

| nRF52840 组件 | BS21 对应 | 复用情况 |
|--------------|----------|---------|
| controller_state.h | 直接复用 | 概念相同 |
| formatter_text/binary | 可参考实现 | BS21 上重新实现 |
| output_cdc/uart | BS21 USB 2.0 CDC | 重新实现 |
| main.c sim_update() | 替换为 SLE 接收 | 核心变更 |

### 5.2 nRF52840 Dongle 的新定位

- nRF52840 Dongle 可用于 USB 抓包辅助验证（通过 PC usbmon）
- nRF52840 radio 不适用于 SLE 通信，暂不使用
- 保留现有 wireless/ 固件作为输出管道的参考实现

## 六、参考资料

- [Ai-BS21-32S-Kit 规格书](https://aithinker-static.oss-cn-shenzhen.aliyuncs.com/docs/Specification/Ai-BS21-32S-Kit_V1.1.0_%20Specification_CN.pdf)
- [Ai-BS21_SDK (GitHub)](https://github.com/Ai-Thinker-Open/Ai-BS21_SDK)
- [Ai-BS21_SDK (Gitee)](https://gitee.com/Ai-Thinker-Open/Ai-BS21_SDK)
- [XFusion 文档](https://www.coral-zone.cc/document/zh_CN/get-started/starting_with_bs21.html)
- [Docker BS21 SDK](https://github.com/lualiliu/bs21_sdk)
- [海思官方 SDK (fbb_bs2x)](https://gitee.com/HiSpark/fbb_bs2x)
- [安信可星闪模组文档](https://docs.ai-thinker.com/sle-bs21/)
- [NearLink ToolBox](https://nearlink.docs.haohanyh.ovh/)
- [海思星闪技术介绍](https://www.hisilicon.com/cn/techtalk/nearlink/introduction)
- [海思开发者平台](https://developers.hisilicon.com/)
