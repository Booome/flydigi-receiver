# Flydigi Receiver Project

## 项目状态

手柄使用**星闪 SLE 1.0 (NearLink)** 进行 2.4GHz 无线通信，正在开发 SLE 接收器。
当前两个开发板平台：
- **Ai-BS21-32S-Kit**（BS21，2 块）— 因 SDK 限制已挂起，后期可能废弃
- **BearPi-Pico H3863**（WS63）— **新主平台**（替代 BS21，性能更强：240MHz/606KB SRAM/Wi-Fi 6），
  开发环境已打通（Hello World 验证），SLE 接收器功能待迁移

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
- 烧录 `bluetooth/esp32-wroom-32e/tools/{build,burn}.py` 调 ESP-IDF；串口抓取走顶层 `tools/capture_uart.py`（与 SLE 共用，端口从 `.env` 的 `BOARD_A_PORT`/`BOARD_B_PORT` 复用，不增键）
- **M9 = 环境搭建**（hello_world 编译/烧录/串口），不涉及手柄；后续 M10/M3+ 起做 BT inquiry、HID 主机连接
- 设计文档：`docs/superpowers/specs/2026-09-05-esp32-env-setup-design.md`，实施计划：`docs/superpowers/plans/2026-09-05-esp32-env-setup.md`

## 平台

### BS21 开发板（已挂起）

详见 `wireless/ai-bs21-32S-Kit/README.md`（芯片规格、SDK、构建、烧录、双模块逆向实验、SDK 陷阱）。

### BearPi-Pico H3863 开发板（新主平台）

详见 `wireless/bearpi-pico-h3863/README.md`（芯片规格、SDK、多 app 结构、构建、烧录）。

### ESP32-WROOM-32E 开发板（蓝牙方向，新平台）

详见 `bluetooth/esp32-wroom-32e/README.md`（芯片规格、ESP-IDF 位置、构建/烧录/串口流程）。

- **非侵入式**：ESP-IDF `/opt/esp-idf` 全程只读；`apps/` 下 example 通过 `cp -r` 复制后再改
- **多 app**：每个 app 是独立 ESP-IDF 项目（顶层 `CMakeLists.txt` + `main/` 组件）；编译产物通过 `-B ../../build/<app>` 落到 board 顶层 `build/<app>/`
- **共享工具**：与 SLE 共用顶层 `tools/capture_uart.py`；ESP32 专用 `tools/{build,burn}.py` 跟随项目

## 烧录与调试（共享工具，两个平台通用）

> **禁止裸跑 ws63flash / screen**，统一用共享工具。

串口/复位配置记录在项目根 `.env`（不入库，模板见 `.env.example`），统一用
`/dev/serial/by-path/` 稳定路径（ttyUSB 编号会漂移，勿硬编码）。

**复位控制**：WS63 reset 引脚通过控制板物理连接，由 `uart-gpio` 命令行工具控制
（不是 STM32 USB-serial 的 DTR/RTS）。`burn.py` / `capture_uart.py` 通过调用
`uart-gpio` 控制复位脉冲。

**共享工具**（顶层 `tools/` + SLE 专用）：
- `tools/capture_uart.py` — 串口抓取（自动连串口 + 可选延迟复位 + 落盘 + 时间戳）。**跨 SLE / 蓝牙两方向通用**——`--board-a` / `--board-b` / `--rst-a` / `--rst-b` 按物理位置识别，端口读 `.env`。
- `wireless/tools/burn.py` — SLE 烧录（ws63flash），SLE 专用。
- `bluetooth/esp32-wroom-32e/tools/burn.py` — ESP32 烧录（idf.py flash），ESP32 专用。

```bash
# 烧录
python3 wireless/tools/burn.py board_a                 # BS21 default app
python3 wireless/tools/burn.py board_a -a sle_probe    # BS21 指定 app
python3 wireless/tools/burn.py board_a <h3863.fwpkg>  # H3863 显式传 fwpkg
python3 bluetooth/esp32-wroom-32e/tools/burn.py        # ESP32 DevKitC（默认 BOARD_A_PORT）

# 抓 log（SLE 板 / ESP32 通用）
python3 tools/capture_uart.py --board-a --board-b --rst-a --duration 60 --odir /tmp --ts
python3 tools/capture_uart.py --board-a --duration 10 --odir /tmp --ts   # ESP32 hello_world 串口
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

### C 代码格式

修改任何 `.c` 或 `.h` 文件后，**必须**使用 `clang-format` 格式化：

```bash
clang-format -i <修改的文件>
```

项目根目录已配置 `.clang-format`（LLVM 风格，4 空格缩进，100 列宽）。
格式化后再提交，确保代码风格一致。

### 禁止 `(void)arg` 抑制 unused-parameter 警告

项目 CMakeLists 已启用 `-Wno-unused-parameter`，**不需要** `unused(arg)` /
`(void)arg;` 来消除 unused-parameter 警告。禁止写此类语句——保持未使用参数裸写即可。

### CMake 格式

`set(VAR value)` 单行写法优先。禁止将单值变量拆成多行：

```cmake
# 正确
set(WHOLE_LINK true)

# 禁止
set(WHOLE_LINK
    true
)
```

### CMake 格式检查

修改任何 `CMakeLists.txt` 后，**必须**使用 `cmake-format` 格式化：

```bash
cmake-format -c .cmake-format.yaml -i <修改的文件>
```

项目根目录已配置 `.cmake-format.yaml`（`max_pargs_hwrap: 1` + `dangle_parens: true`）：
- 单值变量单行：`set(WHOLE_LINK true)`
- 多值变量每项一行，`)` 单独一行：
```cmake
set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sle_server.c
)
```
