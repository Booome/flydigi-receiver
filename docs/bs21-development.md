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
海思官方型号名为 **BS21E**，为同一颗芯片，硬件版本 **N1100**，规格一致
（160KB RAM / 1MB flash / SLE 1.0 12Mbps）。

### 2.1 SDK 选型（2026-08 调研）

| 方案 | License | 结论 |
|------|---------|------|
| **Ai-BS21_SDK** (选用) | 无 license | `bs21-n1100-rcu` SLE-only target + `bs21-rcu` 分区表，512KB flash 可容纳 SLE |
| HiSilicon fbb_bs2x | Apache-2.0 | 最活跃，但闭源 loaderboot 不支持安信可板子 SiP flash，无法烧录运行 |
| XFusion | Apache-2.0 | 社区工具，已停更，未采用 |
| Docker (lualiliu/bs21_sdk) | 无 license | 几乎无人用，未采用 |

**开发模式为只读引用**：SDK 只引用不修改，我们的代码在 `wireless/bs21/`（`apps/`、
`sdk-compat/`、`CMakeLists.txt`、`toolchain.cmake`、`scripts/`），构建时由顶层
`CMakeLists.txt` 直接引用 SDK 的构建脚本与源目录，构建产物收集到 `wireless/bs21/build/`，
SDK 树不叠加源码。

- `apps/`：多工程应用入口，由顶层 `-DBS21_APP=` 选择（默认 `default`）。各工程的
  `main.c` 提供 `axk_main()`（SDK 闭源 `libmain_init_porting.a` 直接调用该符号作为
  用户入口）；`default` 当前复位 GPIO21 + 创建 `hello_task` 循环打印，`g_scanner` /
  `t_broadcaster` 分别为 SLE 扫描器 / 广播器
- `sdk-compat/`：`bs21-n1100-rcu`（SLE-only）的 `libbth_sdk.a` 残留 36 个 `sapi_ble_*`
  BLE 符号引用（无实现），由 `ble_stub.c` 空实现补齐以满足链接

### 2.2 Ai-BS21_SDK 环境搭建（Linux）

SDK 位置：`~/.local/Ai-BS21_SDK`（从 GitHub 克隆）。

```bash
# 一次性前置（仅全新 SDK clone 后需要）：恢复工具链 exec 位 + 补 LiteOS .a symlink
wireless/bs21/scripts/setup-sdk.sh

# 构建（configure + build 两步）
cmake -S wireless/bs21 -B wireless/bs21/build
cmake --build wireless/bs21/build -j
```

- 依赖：Python 3.8+（实测 3.14 需 `pip install setuptools` 提供 distutils）、RISC-V 工具链（SDK 自带）
- `setup-sdk.sh`：一次性环境修复（git clone 会丢失工具链 `+x` 位；`bs21-n1100-rcu` 只含 libc/libm 预编译库，其余从 `standard-bs21-n1100` symlink），不改源码
- `gen-config.py`：构建时生成 SDK target 配置（复用 SDK 的 `TargetEnvironment`），并注入 `NO_BOOT_BACKUP` 修复 flash 布局 bug（见 §2.4）
- 产出 fwpkg：`wireless/bs21/build/`（`bs21_all_in_one.fwpkg` 等）

### 2.2.1 SDK 只读的已知限制

SDK 自带的 sign/packet 工具把部分中间产物硬编码写回 SDK 树，无法只靠"不修改源码"完全规避：

| 写入位置 | git 状态 | 说明 |
|---------|---------|------|
| `SDK/output/` | 已 gitignore | 签名/打包产物（`application_sign.bin`、各 fwpkg） |
| `SDK/interim_binary/` | tracked，字节一致 | 分区/flashboot/loaderboot/nv 等预编译二进制，构建时重新生成但内容不变 |
| `SDK/tools/pkg/fwpkg/bs21/bs21_all.fwpkg` | tracked，构建后变 `M` | packet.py 硬编码写入；构建脚本打包后自动 `git checkout` 恢复 |
| `SDK/build/config/target_config/bs21/fota/__pycache__/*.pyc` | tracked，构建后变 `D` | `gen-config.py` 复用 SDK `TargetEnvironment`，其 import 时 `rm_pyc` 删目标目录 pycache；`package` 目标构建后自动 `git checkout` 恢复 |

前三项内容稳定、不会让 `git status` 变脏；后两项由 `wireless/bs21/CMakeLists.txt` 的
`package` 目标在打包完成后自动 `git checkout --` 恢复。这是只读方案的已知局限——彻底
隔离需给 sign/packet 工具增加输出重定向，超出当前范围。

### 2.3 SDK 架构（Ai-BS21_SDK）

| 目录 | 内容 |
|------|------|
| `application/` | 示例与 demo（`demo/`、`samples/products/sle_uart/` 等） |
| `drivers/` | 驱动库（含闭源 `libmain_init_porting.a` 等） |
| `kernel/` | LiteOS 内核（`bs21-n1100-rcu` 只含 libc/libm，其余从 standard symlink） |
| `build/` | 构建脚本 + target 配置（`config.py`、`flash_sector_config/`） |
| `tools/pkg/` | fwpkg 打包脚本（纯 Python，开源） |
| `interim_binary/` | 预编译 loaderboot/flashboot/partition |

SLE API 头文件开放，协议栈为预编译静态库（`libbth_sdk.a` 等，SLE-only）。

### 2.4 SLE 角色配置

本项目需要配置为 **G 节点**（SLE Central）：

```
CONFIG_SUPPORT_SLE_CENTRAL=y
```

> `bs21-n1100-rcu` 为 SLE-only（`CONFIG_BT_SLE_ONLY`）。**SDK bug**：该 target 的
> `sector_cfg='bs21-rcu'`（application @ 0xb000，无 flashboot_backup）但 defines 漏了
> `NO_BOOT_BACKUP`，导致链接脚本按 standard 布局（application @ 0x15000），flashboot
> 跳错 0xA000 致 application 不启动。`gen-config.py` 已通过 `extra_defines` 注入
> `NO_BOOT_BACKUP` 修复。

### 2.4.1 镜像签名完整性（GENERAT_SEC_IMAGE）

sign 工具 `sign_tool_pltuni` 生成的 `application_sign.bin` 缺少 64 字节安全尾部，需再经
`riscv32-linux-musl-objcopy --enable_sec`（依赖 `libsec_image.so`）追加 sec 信息，否则
flashboot 校验失败、板子 `boot.` 循环重启。顶层 `CMakeLists.txt` 已通过 `GENERAT_SEC_IMAGE`
目标在 sign 后自动执行该步（含 `libsec_image.so` 复制），CMake 产物与 SDK `build.py` 逐字节一致。

### 2.5 烧录（Linux，基于社区 ws63flash）

官方 BurnTool 仅 Windows。社区 [goodspeed34/ws63flash](https://github.com/goodspeed34/ws63flash)
（GPLv3）逆向 BurnTool 实现了 Linux 原生烧录：

```bash
# 安装（Linux）
git clone https://github.com/goodspeed34/ws63flash.git
cd ws63flash && autoreconf -fi && ./configure && make && sudo make install

# 烧录（CH340 串口，Linux 原生 ch341 驱动）
# 实测 921600 不稳定，460800 稳定
ws63flash --flash /dev/ttyUSB1 /path/to/xxx.fwpkg -b460800
```

**fwpkg 格式**（BS2X 与 WS63 一致，同为海思 FBB 框架）：

```
FWPKG_HEAD (12B):  magic=0xefbeaddf + crc16 + imageNum + totalSize
IMAGE_INFO (52B):  name[32] + offset + imageSize + burnAddr + burnSize + type
```

ws63flash 已实现 fwpkg 解析（`src/fwpkg.h`）和 WS63 串口烧录协议（帧头 `EF BE AD DE` +
命令 `0xf0` 握手 / `0x5a` 设波特率 / `0xd2` 下载 / `0x87` 复位）。

**已实测确认**：烧录协议兼容（握手 0xf0 + loaderboot ymodem），460800 稳定。

#### 自动烧录脚本 `wireless/bs21/tools/burn.py`

基于上面手动流程封装的状态机自动烧录工具（输出全英文，依赖 `pyyaml`，见工程根
`requirements.txt`）：

```bash
python3 wireless/bs21/tools/burn.py board_a   # 或 board_b；可选第 2 参指定 fwpkg
```

- 串口/复位配置全部在 `wireless/bs21/tools/burn_config.yaml`（by-path 稳定路径，
  不入库；模板见同目录 `burn_config.yaml.example`）。
- 单轮状态判定：直接跑 `ws63flash`（pty 实时输出）→ 等 `Waiting for device reset` →
  等 2s 有自动下载 = **boot. 循环态**；无则脉冲复位等 1s 有下载 = **正常态**；仍无 =
  **卡死态**（复位无效，只能手动拔插模块 USB 电源恢复）。
- 串口占用自动检查（lsof/fuser → SIGKILL），ttyUSB 编号漂移不影响（始终用 by-path）。

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

### M5: SLE 扫描验证

**目标**：确认 BS21 能扫描到手柄的 SLE 广播

- [x] Ai-BS21_SDK 环境搭建（Linux 编译 + ws63flash 烧录）
- [x] app 启动验证（编译 → 烧录 → 启动 → 串口打印全链路打通；`app` 组件替换 SDK demo 作为入口）
- [x] 修复 `bs21-n1100-rcu` flash 布局 bug（`NO_BOOT_BACKUP`）
- [x] 修复 CMake 构建 `boot.` 循环（补 `GENERAT_SEC_IMAGE`：`objcopy --enable_sec`）
- [x] 两块 BS21 Kit 互相验证 SLE 通信（排除环境问题）
- [x] BS21 配置为 G 节点，执行 SLE 扫描
- [x] 手柄开机（PC模式），观察扫描结果
- [x] 记录手柄 SLE 地址、RSSI
- [ ] 记录手柄广播数据（载荷解析留待 M7）
- [x] 输出方式：USB2 串口输出扫描日志

> **已完成**：`default` app 已改造为 SLE 扫描器（G 节点），按地址聚合统计、每 2 秒
> 打印设备表（`[scan] devices:N` + 每设备一行）。board-to-board 对测 + 手柄识别均通过，
> 通过开关手柄观察计数停增锁定手柄地址。
> 设计见 `docs/superpowers/specs/2026-08-18-default-sle-scan-design.md`，
> 实现计划见 `docs/superpowers/plans/2026-08-18-default-sle-scan.md`。

**识别结果（2026-08-18）**：
- 手柄 SLE 地址：`a1:a2:c8:75:43:b8`（开机持续广播，关机停止广播）
- 广播 RSSI：约 -43 ~ -49 dBm（手柄距 board_b ~30cm；板对板对测 -21 ~ -26）
- 扫描输出走 USB2 串口：`[scan] devices:N` + `  i) aa:bb:cc:dd:ee:ff rssi:-XX cnt:NN`
- 本阶段 seek 仅聚合地址/RSSI/计数，广播载荷解析留待 M7

**成功标准**：扫描到手柄 SLE 广播，获取地址和广播数据

### M6: SLE 连接尝试

**目标**：尝试与手柄建立 SLE 连接

- [x] 基于 M5 获取的地址，发起连接（`sle_connect_remote_device`）
- [x] 观察连接状态变化
- [x] 如果连接建立，尝试配对（`sle_pair_remote_device`）
- [x] 记录连接/配对过程中各阶段的状态
- [ ] 如果配对被拒，分析拒绝原因（地址过滤？SMP 密钥？）——本次配对成功，未触发

> **已完成**：`default` app 改造为全自动连接状态机（SCAN → CONNECTING → PAIRING →
> ACTIVE，回调驱动，`seek_result_cb` 锁定目标地址后 `sle_stop_seek`，`seek_disable_cb`
> 发起 `sle_connect_remote_device`，连接后按需 `sle_pair_remote_device`）。
> board-to-board 对测（`t_broadcaster` 非零地址 `aa:bb:cc:dd:ee:01`）+ 手柄连接均成功。
> 设计见 `docs/superpowers/specs/2026-08-18-m6-connection-design.md`，
> 实现计划见 `docs/superpowers/plans/2026-08-18-m6-connection.md`。

**连接结果（2026-08-18）**：
- 手柄连接成功：锁定 `a1:a2:c8:75:43:b8`（RSSI -53）→ `CONNECTED`（conn id:0, state:1）→
  `pairing...` → `paired:0x0`（配对成功）→ 稳定保持 48s+（seek 停止，计数冻结）
- board-to-board：`t_broadcaster`（`aa:bb:cc:dd:ee:01`）持续广播（g_scanner 收到 1688 帧/30s），
  连接+配对成功，连接后 seek 自动停止、无重连
- 连接参数使用 SDK 默认（未调用 `sle_default_connection_param_set`）

**已知问题（非阻断）**：
- `default` 复位后第一遍 seek 偶发 ULP 复位（`Reboot cause:0x2010`，ULP 引脚强制复位），
  system reboot 后第二遍稳定；板对板与手柄测试中第二遍均正常完成连接。根因待查（疑似
  SDK/controller 层或复位早期射频初始化问题，未在应用代码路径）
- 配对成功日志为 `paired:0x0`（`ERRCODE_SUCC`），但状态机 `pair_complete_cb` 成功路径
  无多余动作（保持 ACTIVE，数据收发留待 M7）

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
- [Ai-BS21_SDK (安信可)](https://github.com/Ai-Thinker-Open/Ai-BS21_SDK)
- [ws63flash (Linux 烧录工具)](https://github.com/goodspeed34/ws63flash)
- [安信可星闪模组文档](https://docs.ai-thinker.com/sle-bs21/)
- [NearLink ToolBox](https://nearlink.docs.haohanyh.ovh/)
- [海思星闪技术介绍](https://www.hisilicon.com/cn/techtalk/nearlink/introduction)
