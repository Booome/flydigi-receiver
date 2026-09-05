# Flydigi Receiver Project

## 项目状态

手柄使用**星闪 SLE 1.0 (NearLink)** 进行 2.4GHz 无线通信。SLE 接收器方向**两个平台均已挂起**，
当前活跃方向是**蓝牙（ESP32）**：
- **Ai-BS21-32S-Kit**（BS21）— 已挂起：SDK 闭源 loaderboot 不支持安信可 SiP flash，无法烧录运行
- **BearPi-Pico H3863**（WS63）— 已挂起：SLE 广播对 BS2x 家族平台级不可见，无法与 dongle/手柄建立 SLE 通信

两平台挂起的详细原因与验证见「## 平台」各节与 `docs/history.md`。

历史尝试（nRF52840 BLE / 2.4GHz 无线）见 `docs/history.md`。

详细分析见：
- `docs/sle-analysis.md` - SLE 协议分析与逆向可行性评估
- `docs/sle-ssap-protocol-findings.md` - SLE/SSAP 协议分析发现（0x16 帧、PDU 格式、Hook 点）
- `wireless/ai-bs21-32s-kit/docs/development.md` - BS21 开发板、SDK 与开发路线图
- `docs/controller-modes.md` - 手柄模式与协议详解
- `docs/history.md` - 项目历史与技术演进记录
- `wireless/bearpi-pico-h3863/docs/design.md` - H3863 开发环境设计文档

### 蓝牙方向（新，与 SLE 独立）

- ESP32-WROOM-32E（ESP32-D0WD-V3，Xtensa LX6 双核 240MHz，rev3.1）
- ESP-IDF v6.0.2（yay AUR `esp-idf`，`/opt/esp-idf`，**不**复制到 `~/workspace`）
- 仅经典 ESP32 在 ESP-IDF 全家族中带 BR/EDR（S2/S3/C3/C5/C6/H2/C61/E22 全 BLE-only），故 ESP32-WROOM-32E 是 BR/EDR HID 主机研究的唯一对口芯片
- 项目在 `bluetooth/esp32-wroom-32e/`（与 `wireless/` 平级），非侵入式编译，多 app（`apps/<app>/` + `build/<app>/`）
- 烧录 `bluetooth/esp32-wroom-32e/tools/{build,burn}.py` 调 ESP-IDF；串口抓取走顶层 `tools/capture_uart.py`（与 SLE 共用，端口从 `.env` 的 `BOARD_A_PORT`/`BOARD_B_PORT` 复用，`BOARD_A_TYPE=esp32-wroom-32e` 选 DTR 复位）
- **环境搭建**（`apps/hello_world/` 编译/烧录/串口）✓
- **BT 双模扫描**（`apps/bt_scan/`，esp_hid_scan 扫到 Apex5 BR/EDR 广播）✓
- **BT HID 主机连接 + 原始报告采集**（`apps/default/` ——项目主 app，后续迭代都更新它）✓
  三层候选算法（COD gamepad 过滤 + EWMA 平滑 + 迟滞/8s 兜底）连上 Apex5，稳定收 15 字节
  Xbox 风格 HID 报告（~36/s，实测 60s 2190 条），原始 hex 打串口。HID 反格式 / 震动输出留后续
- 设计文档：`docs/superpowers/specs/2026-09-05-esp32-env-setup-design.md` / `2026-09-05-bt-scan-design.md` / `2026-09-05-bt-hid-host-capture-design.md`

#### **里程碑命名约定（2026-09 修订）**

不再用 `M<n>` 编号。理由：换板时（不同物理槽位、不同 BLE/SLE 板混插）`M<n>` 与具体板的对应会乱。改用**实际工作内容命名**：
- `apps/hello_world/` = 环境基线（hello world 验证）
- `apps/bt_scan/` = BT 双模扫描工具
- `apps/default/` = 项目主 app（持续迭代）
- spec / plan / 分支名 = `<topic>-<action>` 形式（如 `bt-hid-host`）

历史内容里 M9/M10/M11 之类命名保留作为 commit message 记录（便于 git log 追溯），但**新里程碑不再用 Mn 编号**。

### BT 双模扫描实测（2026-09-05）飞智八爪鱼5 蓝牙广播

ESP32 (`apps/bt_scan`) 实测扫到（手柄在 BT 模式 + 配对状态）：

| 字段 | 值 |
|---|---|
| MAC | `b5:5d:e7:98:54:75` |
| NAME | `Xbox Wireless Controller`（BR/EDR 侧就用这个名字，不是"Flydigi Apex5"）|
| COD | major = PERIPHERAL (5)，minor = 2（gamepad/joystick）|
| UUID | 0x1124（HID over BR/EDR L2CAP）|
| RSSI | -47 ~ -54 dBm（28s 实测，噪声 ±3~5 dB）|
| BR/EDR 命中 | 15 次（28s）|
| BLE 命中 | **0 次**（手柄不广播 BLE——**手柄是单模 BR/EDR**，纠正先前"双模蓝牙"猜测）|
| 周边干扰 | 仅有 Apple 设备（`U-ACGDDEC`/`U-ACGA332` 等），COD 非 gamepad，不构成竞争 |

详见 `bluetooth/esp32-wroom-32e/apps/bt_scan/README.md` 验证结果节、`docs/controller-modes.md` 三节。

### 手柄操作经验（重要）

- **手柄空闲时极快进入省电模式**：手柄不进配对状态、不使用时 10-30 秒内就停止 BR/EDR 可发现广播。每次准备测试手柄相关功能（scan、connect）前，先**提醒用户**确认手柄：
  1. 拨杆到中间（蓝牙 / 蓝色 LED 位置）
  2. 长按配对键进入可发现状态（蓝色 LED 快闪）
  3. 用户回复"开好了"后再跑命令
  4. 常见错误：以为手柄没开 → 实测已关省电 → 30s 扫不到 → 误判为协议问题

- **手柄断开会回到候选阶段**：ESP32 HID 主机收到 `ESP_HIDH_CLOSE_EVT` 后会自动清状态、回扫描循环（不需手动重连）。

- **手柄 BT 身份随机身「连接模式」变化**（存在手柄里、掉电保留，误触菜单/组合键会翻，恢复出厂回默认 X-input）：
  | 模式 | 蓝牙名 | 实质 |
  |---|---|---|
  | PC>蓝牙 / Android / iOS（X-input）| `Xbox Wireless Controller` | 飞智 X-input HID（我们主线目标）|
  | FlashPlay | `Flydigi Apex5` | 飞智映射 |
  | Switch | `Pro Controller` | 伪装成任天堂 Pro Controller（`057e:2009`），不同 HID 协议/不同 BD_ADDR |

  `apps/default/` 的候选筛选**按名字白名单**（`g_gamepad_names`），当前含 Xbox + Pro Controller 两个实测身份；**后期出现新名字再加**（不排除变其它名）。按 `strcasecmp` 全名匹配 + `transport==BT`。

## 平台

### BS21 开发板（已挂起）

详见 `wireless/ai-bs21-32S-Kit/README.md`（芯片规格、SDK、构建、烧录、双模块逆向实验、SDK 陷阱）。

### BearPi-Pico H3863 开发板（已挂起）

详见 `wireless/bearpi-pico-h3863/README.md`（芯片规格、SDK、多 app 结构、构建、烧录）。

**挂起原因**：WS63 的 SLE 广播对 BS2x 家族（真机 dongle、手柄 SLE 芯片同源）平台级不可见，
无法与 dongle/控制器建立 SLE 通信。信道掩码问题可用 NV `btc_channel_scan_switch=0` 修，
但修完仍扫不到——根因在控制器同步层以下，穷尽 host 侧配置无解（详见 `docs/history.md`、
如般微/海思官方"广播格式仅支持手机星闪扫描"记录）。编译/烧录环境本身可用，SLE 接收器链路不通。

### ESP32-WROOM-32E 开发板（蓝牙方向，新平台）

详见 `bluetooth/esp32-wroom-32e/README.md`（芯片规格、ESP-IDF 位置、构建/烧录/串口流程）。

- **非侵入式**：ESP-IDF `/opt/esp-idf` 全程只读；`apps/` 下 example 通过 `cp -r` 复制后再改
- **多 app**：每个 app 是独立 ESP-IDF 项目（顶层 `CMakeLists.txt` + `main/` 组件）；编译产物通过 `-B ../../build/<app>` 落到 board 顶层 `build/<app>/`
- **共享工具**：与 SLE 共用顶层 `tools/capture_uart.py`；ESP32 专用 `tools/{build,burn}.py` 跟随项目

## 烧录与调试（共享工具，SLE / 蓝牙跨方向通用）

> **禁止裸跑 ws63flash / screen**，统一用共享工具。

串口/复位配置记录在项目根 `.env`（不入库，模板见 `.env.example`），统一用
`/dev/serial/by-path/` 稳定路径（ttyUSB 编号会漂移，勿硬编码）。

**复位控制**：每块板的复位机制由 `.env` 的 `BOARD_<X>_TYPE` 决定（默认 `ai-bs21-32s-kit`，即 SLE 板）：
- `ai-bs21-32s-kit` / `bearpi-pico-h3863` —— reset 引脚接控制板，`tools/capture_uart.py` 通过 `uart-gpio` 命令行工具控制复位脉冲。需要 `.env` 里的 `BOARD_<X>_RST_PORT` / `BOARD_<X>_RST_PIN`。
- `esp32-wroom-32e` —— 板载 USB-UART 桥已把 DTR 接 EN，`tools/capture_uart.py` 直接 toggle DTR 复位（无需 `uart-gpio` / ctrl pin）。RST_PORT/PIN 不需要。
未知值会警告并回落到 `ai-bs21-32s-kit`。`wireless/tools/burn.py`（ws63flash）始终走 `uart-gpio`，与 BOARD_TYPE 无关。

**共享工具**（顶层 `tools/` + SLE 专用）：
- `tools/capture_uart.py` — 串口抓取（自动连串口 + 可选延迟复位 + 落盘 + 时间戳）。**跨 SLE / 蓝牙两方向通用**——`--board-a` / `--board-b` / `--rst-a` / `--rst-b` 按物理位置识别，端口读 `.env`。复位机制按 `BOARD_<X>_TYPE` 分支。
- `wireless/tools/burn.py` — SLE 烧录（ws63flash），SLE 专用。
- `bluetooth/esp32-wroom-32e/tools/burn.py` — ESP32 烧录（idf.py flash），ESP32 专用。

```bash
# 烧录
python3 wireless/tools/burn.py board_a                 # BS21 default app
python3 wireless/tools/burn.py board_a -a sle_probe    # BS21 指定 app
python3 wireless/tools/burn.py board_a <h3863.fwpkg>  # H3863 显式传 fwpkg
python3 bluetooth/esp32-wroom-32e/tools/burn.py        # ESP32 DevKitC（默认 BOARD_A_PORT）

# 抓 log：SLE 板（默认 BOARD_A_TYPE=ai-bs21-32s-kit 或 bearpi-pico-h3863，uart-gpio 复位）
python3 tools/capture_uart.py --board-a --board-b --rst-a --duration 60 --odir /tmp --ts

# 抓 log：ESP32 DevKitC（需在 .env 加 BOARD_A_TYPE=esp32-wroom-32e，DTR 复位）
python3 tools/capture_uart.py --board-a --rst-a --duration 10 --odir /tmp --ts
```

- burn.py 状态机：跑 ws63flash（pty 实时输出）→ 等 2s 判定 boot. 循环态；无则脉冲复位
  等 1s 判定正常态；仍无 = 卡死态（复位无效，只能手动拔插模块 USB 电源恢复）。
- capture_uart.py：board_a/board_b 可选，至少选一个；Ctrl+C 优雅保存（即使未到 duration
  也会把已收字节落盘）；需要较长监听时增大 `--duration`（如 `--duration 600`）。

reset 轮特征（靠内容区分每轮）：
- 每轮从 `boot.` → `Flashboot Init!` 开始
- 标志行：`Unkown Boot Type 0xDEAD000D`、`Jump to app! addr = 0x9010B300`、`Debug uart init succ:80000`
- 应用起点：`app: <工程名>`（`default` 为 `flydigi-wireless`）

## 手柄硬件信息

- 型号：飞智八爪鱼5 (Flydigi Apex 5)
- FCC ID：2AORE-K5
- 2.4GHz 芯片：P352903N1（星闪 SLE 1.0，飞智定制编号）
- 蓝牙芯片：BP1Y303-D4（BR/EDR）
- USB VID/PID：0x37D7 / 0x2501

## 用户协同操作提示音

当任务需要用户**联合操作**（如连接/断开手柄、拔插 USB 电源、按复位键等）时，
**先播放提示音通知用户，再给出文字操作说明**。不要静默等待用户自己发现需要操作。

此外，**每当完成一段工作、进入空闲等待用户下一步指令时**，也要播放一次提示音
通知用户"我可以接收新任务了"，并更新本地记忆（如本文件 / 相关 docs）以反映
当前进展。**不要静默卡在空闲态**。

提示音脚本：`tools/notify.sh`（播放 `notify_alarm.wav`，一段明显的
三连蜂鸣 + 收尾高音，三角波 vol=0.70 + 10ms 淡入淡出包络，无破音）。重新生成用
`tools/notify_gen.py`。播放依赖 `paplay`（PulseAudio），无音频环境会报错退出。

调用示例：
```bash
bash tools/notify.sh
# 然后打印："请连接手柄并进入 2.4GHz SLE 配对模式"
```

### 硬件连接切换规则（重要）

当需要**切换硬件连接配置**时（例如从"board_a + board_b 互测"切换到"插真机
dongle"、拔插某块板的 USB 电源、改接串口线等），**必须先播放提示音，然后等待
用户完成操作并确认，再继续执行命令**。

不要假设硬件还在之前的配置状态。每次涉及物理连接变更，都要：
1. 播放提示音
2. 明确说明需要用户做什么（哪块板、拔还是插、操作哪个接口）
3. 等用户回复"好了"/"完成"后再跑命令

反例（禁止）：在 board_a + board_b 互测进行中，突然要求用户插真机 dongle 抓包，
却不说明切换原因和操作步骤。

## 开发规范

### Git worktree（项目修改一律在 worktree 中进行）

任何项目修改（代码/文档/配置）都必须在隔离 worktree 中进行，主工作区保持干净。
worktree 统一建立在**工程根 `.worktrees/` 目录内**（已被 `.gitignore` 忽略），
不要建在工程目录外。命名建议用分支名：`.worktrees/<branch>`。

```bash
git worktree add .worktrees/<branch> -b <branch>
git worktree list            # 查看所有 worktree
git worktree move <old> .worktrees/<branch>   # 移动已有 worktree 到 .worktrees/
```

### 防"掩耳盗铃"式修复（重要，适用所有代码修改）

观察者的解析/显示/返回值**不等于数据真相**——可能由观察者侧机制产生
（base UUID 组装、回填、缓存、固定长度等）。禁止"只让观察者显示变对"
的修复。本项目的惨痛案例（uuid len=2 假象掩盖 value=0000）见
`wireless/ai-bs21-32s-kit/docs/ssap-uuid-false-fix.md`。

**铁律**（适用一切代码修改，不只协议/逆向）：
1. **修改前先验证"真实数据"**：任何让显示/返回值"对齐"的修改，必须先拿到
   真实的源数据（线上字节、存储值、真实输入），与观察值**双侧 diff**。
   若修改只改变观察者打印/返回的内容，而底层数据仍错——这是掩耳盗铃，禁止。
2. **先逆向观察者侧机制，再修生产者侧**：当观察值不随真实输入变化（固定
   长度、固定前缀等），立即深挖观察者侧如何产生该值，不得绕过。
3. **patch/改码前必须理解双侧机制**：确认改动改变的是"真实数据"而非
   "显示路径"。
4. **回归必须覆盖真实数据**：`regress_find.py` 验证 probe 显示通过 ≠ 线上
   字节一致，需要时补充原始字节对比断言。

### 保持仓库代码始终已格式化（重要，适用所有代码）

仓库里所有 `.c/.h` 与 `CMakeLists.txt` **必须始终处于格式化状态**，任何时刻都不留未格式化代码。

**时机（何时格式化）**：
1. **每次编辑后**：改任何 `.c`/`.h` 立即 `clang-format -i <文件>`；改 `CMakeLists.txt` 立即 `cmake-format -c .cmake-format.yaml -i <文件>`。不攒到提交前。
2. **提交前（强制闸门）**：commit 前对涉及文件跑格式化；拿不准就全量刷（命令见下）。格式化改动与代码改动一起提交。
3. **合并 / rebase 后**：跨分支合并会带回不同格式副本，合并完立即全量刷一遍再提交。
4. **审阅 diff 前**：先格式化再看，避免逻辑改动被格式噪声淹没。

**全量格式化（tracked，跳过 build/ 与 docs/reference/）**：
```bash
find . \( -name '*.c' -o -name '*.h' \) \
  -not -path './.git/*' -not -path '*/build/*' -not -path './docs/reference/*' \
  -print0 | xargs -0 clang-format -i
find . -name CMakeLists.txt -not -path './.git/*' -not -path '*/build/*' \
  -print0 | xargs -0 cmake-format -c .cmake-format.yaml -i
```

**提交前校验（有未格式化即非 0 退出，可作 pre-commit 闸门）**：
```bash
find . \( -name '*.c' -o -name '*.h' \) -not -path './.git/*' -not -path '*/build/*' \
  -not -path './docs/reference/*' -print0 \
  | xargs -0 clang-format --dry-run --Werror
```

**无豁免**：从 ESP-IDF example `cp -r` 进来的副本（`apps/*/main/esp_hid_gap.c`、
`apps/hello_world/main/main.c` 等）也一并格式化（2026-09 起取消豁免）——格式化只动空白 /
换行、不改语义；重新 `cp` 上游后按本节再刷一次即可。

### 禁止 `(void)arg` 抑制 unused-parameter 警告

项目 CMakeLists 已启用 `-Wno-unused-parameter`，**不需要** `(void)arg;` 来消除 unused-parameter 警告。禁止写此类语句——保持未使用参数裸写即可。
