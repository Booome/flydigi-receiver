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

**开发模式为只读引用**：SDK 只引用不修改，我们的代码在 `wireless/ai-bs21-32s-kit/`（`apps/`、
`sdk-compat/`、`CMakeLists.txt`、`toolchain.cmake`、`scripts/`），构建时由顶层
`CMakeLists.txt` 直接引用 SDK 的构建脚本与源目录，构建产物收集到 `wireless/ai-bs21-32s-kit/build/`，
SDK 树不叠加源码。

- `apps/`：多工程应用入口，由顶层 `-DBS21_APP=` 选择（默认 `default`）。各工程的
  `main.c` 提供 `axk_main()`（SDK 闭源 `libmain_init_porting.a` 直接调用该符号作为
  用户入口）；SLE 角色在工程目录的 `config` 文件声明（如 `sle_role = central`），
  `gen-config.py` 自动读取，新增工程无需改脚本，缺省为 SDK 默认（peripheral）；
  `default` / `g_scanner` / `sle_*` 为 central（扫描/连接方），`t_broadcaster` 为
  peripheral（广播方）
- `sdk-compat/`：`bs21-n1100-rcu`（SLE-only）的 `libbth_sdk.a` 残留 36 个 `sapi_ble_*`
  BLE 符号引用（无实现），由 `ble_stub.c` 空实现补齐以满足链接
- `common/`：跨工程共享的 SLE 连接工具模块，各 app 在 `CMakeLists.txt` 里通过
  `${CMAKE_SOURCE_DIR}/common/` 引用（注意不可用相对 `../common/`，SDK 构建脚本会
  错误规范化路径）。含 `bs21_util`（`bs21_rst` 引脚初始化含 `0x5702C51C` 底层寄存器
  配置、`sle_scan_start`、本地地址设置）、`scan_table`（扫描设备表）、
  `controller_state`（手柄状态定义）

### 2.2 Ai-BS21_SDK 环境搭建（Linux）

SDK 位置：`~/.local/Ai-BS21_SDK`（从 GitHub 克隆）。

```bash
# 一次性前置（仅全新 SDK clone 后需要）：恢复工具链 exec 位 + 补 LiteOS .a symlink
wireless/ai-bs21-32s-kit/scripts/setup-sdk.sh

# 构建（configure + build 两步）
cmake -S wireless/ai-bs21-32s-kit -B wireless/ai-bs21-32s-kit/build
cmake --build wireless/ai-bs21-32s-kit/build -j
```

- 依赖：Python 3.8+（实测 3.14 需 `pip install setuptools` 提供 distutils）、RISC-V 工具链（SDK 自带）
- `setup-sdk.sh`：一次性环境修复（git clone 会丢失工具链 `+x` 位；`bs21-n1100-rcu` 只含 libc/libm 预编译库，其余从 `standard-bs21-n1100` symlink），不改源码
- `gen-config.py`：构建时生成 SDK target 配置（复用 SDK 的 `TargetEnvironment`），并注入 `NO_BOOT_BACKUP` 修复 flash 布局 bug（见 §2.4）
- 产出 fwpkg：`wireless/ai-bs21-32s-kit/build/`（`bs21_all_in_one.fwpkg` 等）

### 2.2.1 SDK 只读的已知限制

SDK 自带的 sign/packet 工具把部分中间产物硬编码写回 SDK 树，无法只靠"不修改源码"完全规避：

| 写入位置 | git 状态 | 说明 |
|---------|---------|------|
| `SDK/output/` | 已 gitignore | 签名/打包产物（`application_sign.bin`、各 fwpkg） |
| `SDK/interim_binary/` | tracked，字节一致 | 分区/flashboot/loaderboot/nv 等预编译二进制，构建时重新生成但内容不变 |
| `SDK/tools/pkg/fwpkg/bs21/bs21_all.fwpkg` | tracked，构建后变 `M` | packet.py 硬编码写入；构建脚本打包后自动 `git checkout` 恢复 |
| `SDK/build/config/target_config/bs21/fota/__pycache__/*.pyc` | tracked，构建后变 `D` | `gen-config.py` 复用 SDK `TargetEnvironment`，其 import 时 `rm_pyc` 删目标目录 pycache；`package` 目标构建后自动 `git checkout` 恢复 |

前三项内容稳定、不会让 `git status` 变脏；后两项由 `wireless/ai-bs21-32s-kit/CMakeLists.txt` 的
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

#### 自动烧录脚本 `wireless/ai-bs21-32s-kit/tools/burn.py`

基于上面手动流程封装的状态机自动烧录工具（输出全英文，依赖 `pyyaml`，见工程根
`requirements.txt`）：

```bash
python3 wireless/ai-bs21-32s-kit/tools/burn.py board_a   # 或 board_b；可选第 2 参指定 fwpkg
```

- 串口/复位配置全部在项目根 `.env`（by-path 稳定路径，不入库；模板见 `.env.example`）。
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

### M6.5: 断连检测与真断电感知实验

**目标**：验证 SLE 连接在对方断电时能否被感知（对手柄场景：手柄需在我们接收器
断电后快速感知并进入重连，官方 Dongle 断电手柄约 2-3s 感知）。

**关键发现（2026-08-19）：外接 reset GPIO 灌电导致"假断电"**：
- Ai-BS21-32S-Kit 模块的 reset 引脚外接控制板（STM32）GPIO，默认**推挽输出 HIGH**。
- 拔掉模块 USB（USB2 串口）后，**reset 引脚从控制板灌入电压，模块并未真正断电**，
  仍维持协议栈运行 → 对端感知不到。此前多次"拔电 90s 不感知"均为此假象。
- **根治：reset 引脚改为 open-drain 输出**（`uart-gpio config <控制串口> A <引脚>
  open-drain`，释放态靠板上拉维持 HIGH）。推挽时灌电由 GPIO 强驱动，open-drain
  释放态灌电被上拉电阻限制，拔电即真断电。STM32 重启后 GPIO 模式会丢，**测试脚本
  开头必须重新 config**。

**测量结果**：
- 稳定连接链路：连接 → 配对 `0x0` → param update `status:0x0 interval:100
  latency:0 superv:200`（superv 生效，实测感知时间随 superv 变化）。
- **双向断链检测均通过（open-drain 释放态，无需写 reset 0）**：
  - **T（被连方）断电 → G（连接方）感知**：拔电后约 **1.9s**（`disc:0x7`）。
  - **G（连接方）断电 → T（被连方）感知**：拔电后约 **1.9s**（`disc:0x7`）。
- 对照组（切断灌电 + superv=4000/40s）：T 断电感知约 5.25s（superv 生效）。
- 推挽默认 HIGH 时仅拔电不切断灌电：对端 90s+ 不感知（模块被灌电未断电）。

**手柄验证（关键）**：
- 手柄（`a1:a2:c8:75:43:b8`）作为 T 连上接收器（G，`sle_pair`，param update
  superv=200）→ 接收器断电 → **手柄约 2s 感知断开**（superv 超时）。
- 结论：**协议栈 supervision 正常，手柄断连感知与官方 Dongle 场景一致**。
  之前"手柄 3 分钟不感知"的结论作废，根因即接收器被 reset 灌电未断电。

**辅助脚本（已入库）**：
- `wireless/ai-bs21-32s-kit/tools/bs21_connect.py`：稳定建立 G↔T 连接（config open-drain →
  先复位 G 释放对端、再复位 T 重新广播 → 等 param update 确认）。

**结论**：
- SLE 对端断电可感知（`disc:0x7`，协议栈链路层超时；不在 `sle_disc_reason_t`
  枚举——该枚举仅 0x10 远端断开 / 0x11 本端断开）。
- 产品化接收器为独立设备、不接开发板 reset 引脚，真断电时手柄可感知；
  如需更快可调小 superv（param update）。

**本次调试中一并修复的问题**：
- **配对失败根因**：`auth_complete_cb` 未注册 → SMP 密钥从未保存
  （`sle_set_nv_smp_keys`）→ 连接方重启后无密钥 → 重新配对 → 被连方有旧密钥拒绝
  （`0x8000600d`/`0x8000600f`）。给 `sle_pair`/`sle_accept` 都注册
  `auth_complete_cb`（认证成功后 `sle_set_nv_smp_keys`）后配对稳定。
- **延迟 seek（3s）**：`sle_pair` 用 `delayed_scan_task` 延迟 3s 再 seek，避免复位后
  立即连接刚广播的对端导致 param update 协商中断 `disc:0x7`。
- **被连方断开后停广播导致无法重连（2026-08-19 fix）**：`sle_accept` 此前断开后
  不再广播（re-announce 曾回退）。表现为"对端（G）重新上电后扫不到 T、无法重连"。
  修复：`conn_state_changed_cb` 中，断开且非本端主动（`disc_reason !=
  SLE_DISCONNECT_BY_LOCAL`）时，创建 `re_announce_task` 延迟 5s 重新
  `sle_start_announce`。
- **验证（重新烧录双方、密钥清空后）**：
  - A（G）断电（hold reset）→ B（T）约 2s 感知 → A 重新上电 → **自动重连成功**
    （配对 `0x0` + param update `superv:200`）。
  - 复位顺序"先复位 B（T）、2s 后复位 A（G）"→ **连接成功**（此前失败，因旧 A
    抢连刚广播的 B 致 B 停广播）。
  - 注意：重新烧录一方会擦除其 SMP 密钥，导致密钥不对称配对失败
    （`0x8000600d` 无限重连循环），需双方都擦除/重烧后重新配对。

**测试注意事项**：
- 测试前先 `uart-gpio config <控制串口> A <引脚> open-drain`（STM32 重启会丢）。
- 复位/连接顺序：先复位 G（释放对端被占用的广播），再复位 T 重新广播。
- 拔电测试前必须先确认连接建立（param update），否则"未感知"是连接未建立的假象。

### M6.6: default app 连接管理与配对逻辑（正式固件）

**目标**：`default` app 从"全自动单次连接演示"升级为正式固件——完整的连接
状态机、就近手柄选择、手动配对与 NV 记录持久化。

实现文件（`wireless/ai-bs21-32s-kit/apps/default/`）：
- `conn_mgr.c`：连接状态机与回调调度（核心逻辑）
- `led.c`：LED 控制（红 IO11 / 蓝 IO13，高电平点亮）
- `conn_nv.c`：NV 连接记录存储 / 擦除
- `button.c`：IO0(S_MGPIO0) 按键检测（短按/长按/超长按）
- `rssi_pick.c`：RSSI 就近选择（滑动滤波 + 持续保持）
- `main.c`：外设初始化、回调注册、任务创建

公共层（`wireless/ai-bs21-32s-kit/common/`）：
- `scan_table.c`：扫描结果聚合表（地址/RSSI/计数）
- `bs21_util.c`：GPIO 复位、扫描启动、本地地址设置

**状态机**（`conn_state_t`）：`FATAL / RECONNECT / SEARCH / ACTIVE`：
- **RECONNECT**：按 NV 记录地址扫描，命中记录地址即锁定连接（忽略 RSSI）。
- **SEARCH**：就近搜索。可带配对超时（长按换新手柄）或不带（无记录初次配对）。
  配对成功会覆盖/保存 NV 记录。
- **ACTIVE**：连接已建立，`pair_complete_cb` 成功后发送 param update
  （superv=200ms），断开回调回 RECONNECT（有记录）/SEARCH（无记录）。
- **FATAL**：NV 真读/写失败重试后置位，阻塞连接流程，红灯常亮。
- 上电流程：初始化外设 → 读 NV → 有记录走 RECONNECT，无记录走 SEARCH。

**按键**（`button.c`）：IO0(S_MGPIO0) 上拉输入，按下拉低。轮询 10ms，
**动作在松开时判定**：
- 按下 < 3s 松开 → 短按：配对超时期间退出配对回连旧设备。
- 按下 3–10s 松开 → 长按：进入 SEARCH（有记录带 120s 配对超时，无记录不带）。
- 按下 ≥ 10s 松开 → 超长按：强制擦除 NV 记录（失败进 FATAL），进入无超时 SEARCH。

**按键 LED 反馈**（按下期间，`led_btn_feedback`）：
- < 3s：不干预，保持当前状态 LED。
- 3–10s：接管蓝灯快闪（125ms），提示已达长按门槛。
- ≥ 10s：蓝灯常亮，提示已到擦除时刻。
- 松开释放接管，恢复状态 LED（统一 systick 时钟，相位连续不抖动）。

**LED**（`led.c`，高电平点亮）：
- 红灯 IO11：FATAL（NV 真读/写失败重试后）常亮。
- 蓝灯 IO13：SEARCH 快闪 125ms；RECONNECT 慢闪 1000ms；ACTIVE 熄灭。

**RSSI 就近选择**（`rssi_pick.c`，参数宏见文件头）：
- `RSSI_THRESHOLD` 50：近场判定阈值（`滤波均值 >= -50 dBm`），待标定。
- 滑动滤波窗口 8（`RSSI_FILTER_WIN`），均值超过阈值且持续保持
  `RSSI_HOLD_MS` 2000ms 才锁定。
- 更强设备带滞后抢占：新设备须比当前候选均值强 `RSSI_SWITCH_DB` 3dB 且持续
  `RSSI_SWITCH_HOLD_MS` 500ms 才切换候选（`rssi_pick_feed` 内部跟踪接管候选并
  维持独立滤波窗口，切换后重置 hold 计时）。
- 失联宽限 `RSSI_LOST_MS` 1000ms：`rssi_pick_tick` 按 tick 超时重置当前候选，
  接管候选则按 `RSSI_SWITCH_HOLD_MS` 重置。

**NV 存储**（`conn_nv.c`）：key `0x3001`，记录 `{valid, addr[6]}`。读失败
重试 3 次；无记录（key 未写 / valid 不符）正常搜索非致命；真读/写失败重试后
置致命标志 → FATAL 红灯常亮。配对成功（无记录 SEARCH / 长按配对 SEARCH）后
保存地址；超长按写无效记录实现擦除。

**交互逻辑**（`conn_mgr.c`）：
- SEARCH 带配对超时 `PAIR_TIMEOUT_MS` 120s，超时自动退出回连旧设备。
- 进入/退出配对搜索时若处于 ACTIVE 会先断开当前链路。
- `pair_complete_cb` 成功路径：SEARCH 带超时且目标为本次目标 → 覆盖 NV 记录；
  无记录 SEARCH → 保存记录；成功后清除配对超时标记保持 ACTIVE。

**已知边界（待板上验证）**：
- RSSI_THRESHOLD=50 为初始值，需按实际摆放距离标定。

**待办（暂缓）**：
- **SMP 密钥持久化（免重新配对）**：实测配对后断开重连/重启仍会重新配对
  （重连时 `pair_state` 回到 NONE，`auth_complete` 不触发，`sle_set_nv_smp_keys`
  未执行；断开时 `pair_state` 为 PAIRED，说明配对状态仅存在于会话内、未持久化）。
  需显式调用 `sle_set_save_pair_keys_mode`（AUTO/MANU）开启密钥保存后再验证，
  且结果还取决于手柄端是否支持 bonding。当前每次连接自动重新配对、流程可用，
  此项延后处理。`conn_mgr_auth_complete` 中的保存逻辑保留（若 auth 触发即可用）。

**测试清单（板上）**：
- [x] 1. 无记录 → SEARCH → 手柄靠近 → 连接+配对 → NV 记录 → 重启自动连旧设备。
- [ ] 2. 长按 IO0 → 蓝灯快闪 → 新手柄靠近 → 配对成功覆盖记录。
- [ ] 3. 配对搜索短按 → 退出 → 回连旧设备。
- [ ] 4. 配对搜索超时 2min → 回连旧设备。
- [ ] 5. 断连 → 回 RECONNECT 自动重连。
- [ ] 6. NV 多次失败 → IO11 红灯常亮（FATAL）。
- [x] 7. 长按 3s 蓝灯快闪提示、10s 常亮、松开判定动作（长按/超长按）。

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

### 5.1 开源参考实现

- **OpenHarmony Nearlink Service**（星闪开源协议栈）
  - 主仓库: https://gitcode.com/openharmony/communication_nearlink_service
  - 镜像: https://github.com/openharmony/communication_nearlink_service
  - 本地路径: `~/workspace/communication_nearlink_service`
  - 内容: SSAP 协议实现、SLE 广播/扫描/连接管理、属性读写、通知机制
  - 架构: 应用层 → 框架层 → 系统服务层 → 驱动层(DLI)
  - 用途: 作为 SLE 协议实现的参考，理解 SSAP 协议细节和状态机
  - 注意: 运行在 OpenHarmony 标准系统上，需适配到 BS21 裸机环境

### 5.2 硬件与 SDK

- [Ai-BS21-32S-Kit 规格书](https://aithinker-static.oss-cn-shenzhen.aliyuncs.com/docs/Specification/Ai-BS21-32S-Kit_V1.1.0_%20Specification_CN.pdf)
- [Ai-BS21_SDK (安信可)](https://github.com/Ai-Thinker-Open/Ai-BS21_SDK)
- [ws63flash (Linux 烧录工具)](https://github.com/goodspeed34/ws63flash)
- [安信可星闪模组文档](https://docs.ai-thinker.com/sle-bs21/)
- [NearLink ToolBox](https://nearlink.docs.haohanyh.ovh/)
- [海思星闪技术介绍](https://www.hisilicon.com/cn/techtalk/nearlink/introduction)
