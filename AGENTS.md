# Flydigi Receiver Project

## 项目状态

手柄使用**星闪 SLE 1.0 (NearLink)** 进行 2.4GHz 无线通信，基于
**Ai-BS21-32S-Kit**（BS21 开发板，2 块）开发 SLE 接收器。

历史尝试（nRF52840 BLE / 2.4GHz 无线）见 `docs/history.md`。

详细分析见：
- `docs/sle-analysis.md` - SLE 协议分析与逆向可行性评估
- `docs/sle-ssap-protocol-findings.md` - SLE/SSAP 协议分析发现（0x16 帧、PDU 格式、Hook 点）
- `docs/bs21-development.md` - BS21 开发板、SDK 与开发路线图
- `docs/controller-modes.md` - 手柄模式与协议详解
- `docs/history.md` - 项目历史与技术演进记录

### 参考开源仓库

- **OpenHarmony Nearlink Service**（星闪开源协议栈）
  - 主仓库: https://gitcode.com/openharmony/communication_nearlink_service
  - 镜像: https://github.com/openharmony/communication_nearlink_service
  - 本地路径: `~/workspace/communication_nearlink_service`
  - 内容: SSAP 协议、SLE 广播/扫描/连接管理、属性读写、通知机制
  - 架构: 应用层 → 框架层 → 系统服务层 → 驱动层(DLI)
  - 用途: 作为 SLE 协议实现的参考，理解 SSAP 协议细节和状态机

## 平台

### BS21 开发板（已到货）

Ai-BS21-32S-Kit，基于 Hi2821 (BS21，海思型号名 BS21E) 芯片：
- SLE 1.0 + BLE 5.4 + USB 2.0
- 双 Type-C：USB1 原生 USB 2.0（HID/CDC），USB2 CH340 串口（烧录/调试）
- SDK: 安信可 **Ai-BS21_SDK**（`~/.local/Ai-BS21_SDK`），只读引用模式（SDK 不修改源码）
- target: `bs21-n1100-rcu`（SLE-only，512KB flash）
- 编译：`cmake -S wireless/ai-bs21-32s-kit -B wireless/ai-bs21-32s-kit/build && cmake --build wireless/ai-bs21-32s-kit/build -j`（一次性前置 `wireless/ai-bs21-32s-kit/scripts/setup-sdk.sh`）
- 开发环境搭建和路线图见 `docs/bs21-development.md`

#### 烧录与调试（统一用共享工具，禁止裸跑 ws63flash / screen）

串口/复位配置记录在项目根 `.env`（不入库，模板见 `.env.example`），统一用
`/dev/serial/by-path/` 稳定路径（ttyUSB 编号会漂移，勿硬编码）。复位 GPIO 由控制串口
（STM32）提供。两个平台（BS21 / H3863）共用同一套 `BOARD_*` 定义，切换平台只需
换 fwpkg，无需改端口。

**共享工具**（`wireless/tools/`，两个平台通用）：
- `burn.py` — 自动烧录（跑 ws63flash + 驱动复位，多状态机判定）
- `capture_uart.py` — 串口抓取（自动连串口 + 可选延迟复位 + 落盘 + 时间戳）

**烧录（统一用 burn.py，禁止直接 ws63flash + uart-gpio 两条命令手动烧）**：
```bash
# BS21：fwpkg 自动定位到 wireless/ai-bs21-32s-kit/build/<app>/bs21_all_in_one.fwpkg
python3 wireless/tools/burn.py board_a                 # default app
python3 wireless/tools/burn.py board_a -a sle_probe    # 指定 app
# H3863：显式传 fwpkg（SDK output），自动用 BOARD_A_RST 的 uart-gpio 复位
python3 wireless/tools/burn.py board_a <h3863_all.fwpkg>
```
- 状态机：跑 ws63flash（pty 实时输出）→ 等 2s 判定 boot. 循环态；无则脉冲复位
  等 1s 判定正常态；仍无 = 卡死态（复位无效，只能手动拔插模块 USB 电源恢复）。
- 底层仍走 `ws63flash`，但复位/重试/端口释放由脚本管理，不要手动分步操作。

**读串口/抓 log（统一用 capture_uart.py，禁止 screen/picocom 裸连）**：
```bash
python3 wireless/tools/capture_uart.py --board-a --board-b --rst-a --duration 60 --odir /tmp --ts
```
- board_a/board_b 可选，至少选一个；--rst-a/--rst-b 对已选板复位；Ctrl+C 优雅保存
  （即使未到 duration，Ctrl+C 也会把已收字节落盘）。
- 需要较长监听时增大 `--duration`（如 `--duration 600`），中途 Ctrl+C 即可优雅保存。

reset 轮特征（靠内容区分每轮）：
- 每轮从 `boot.` → `Flashboot Init!` 开始
- 标志行：`Unkown Boot Type 0xDEAD000D`、`Jump to app! addr = 0x9010B300`、`Debug uart init succ:80000`
- 应用起点：`app: <工程名>`（`default` 为 `flydigi-wireless`）

### 用户协同操作提示音

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

#### 硬件连接切换规则（重要）

当需要**切换硬件连接配置**时（例如从"board_a + board_b 互测"切换到"插真机
dongle"、拔插某块板的 USB 电源、改接串口线等），**必须先播放提示音，然后等待
用户完成操作并确认，再继续执行命令**。

不要假设硬件还在之前的配置状态。每次涉及物理连接变更，都要：
1. 播放提示音
2. 明确说明需要用户做什么（哪块板、拔还是插、操作哪个接口）
3. 等用户回复"好了"/"完成"后再跑命令

反例（禁止）：在 board_a + board_b 互测进行中，突然要求用户插真机 dongle 抓包，
却不说明切换原因和操作步骤。

### BearPi-Pico H3863 开发板（已到货）

BearPi-Pico H3863，基于 WS63 (H3863) 芯片：
- SLE 1.0 + BLE 5.4 + Wi-Fi 6
- SDK: 海思 **fbb_ws63**（`~/workspace/fbb_ws63`），只读引用模式（SDK 不修改源码）
- target: `ws63-liteos-app`（LiteOS, acore, 应用处理器）
- 构建：`FBB_APP=default bash wireless/bearpi-pico-h3863/scripts/build.sh`（多 app，`FBB_APP` 选择 `apps/<app>/`）
- 产物：`$FBB_SDK_DIR/output/ws63/fwpkg/ws63-liteos-app/ws63-liteos-app_all.fwpkg`
- 烧录（用共享 burn.py，自动 uart-gpio 复位）：`python3 wireless/tools/burn.py board_a $FBB_SDK_DIR/output/ws63/fwpkg/ws63-liteos-app/ws63-liteos-app_all.fwpkg`
- 抓 log：`python3 wireless/tools/capture_uart.py --board-a --duration 60 --odir /tmp --ts`
- 开发环境搭建见 `docs/superpowers/specs/2026-09-01-bearpi-pico-h3863-design.md`

> **注意**：SDK 的 out-of-tree 构建硬编码查找工程根 `main/CMakeLists.txt` 并把 `main`
> 注册进 RAM_COMPONENT。因此 `main/CMakeLists.txt` 是转发器，通过 `FBB_APP` 环境变量
> 选择 `apps/<app>/`；各 app 的 CMakeLists 必须 `set(COMPONENT_NAME "main")` 才能被链接。

## 手柄硬件信息

- 型号：飞智八爪鱼5 (Flydigi Apex 5)
- FCC ID：2AORE-K5
- 2.4GHz 芯片：P352903N1（星闪 SLE 1.0，飞智定制编号）
- 蓝牙芯片：BP1Y303-D4（BR/EDR）
- USB VID/PID：0x37D7 / 0x2501

## 双模块调试分工（M8 逆向阶段）

固定角色，避免混淆：
- **board_a = 接收器侧**：烧 `sle_probe`（client，扫描/连接/发现/读写实验）
- **board_b = 手柄侧**：烧 `flydigi_decoy`（server，镜像手柄属性表，记录 dongle 行为）

双 build 目录（协议栈库随 sle_role 不同，不能共用）：
```bash
cmake -S wireless/ai-bs21-32s-kit -B wireless/ai-bs21-32s-kit/build-decoy -DBS21_APP=flydigi_decoy
cmake -S wireless/ai-bs21-32s-kit -B wireless/ai-bs21-32s-kit/build-probe -DBS21_APP=sle_probe
# 烧录（显式指定 fwpkg）：
python3 wireless/tools/burn.py board_b wireless/ai-bs21-32s-kit/build-decoy/bs21_all_in_one.fwpkg
python3 wireless/tools/burn.py board_a wireless/ai-bs21-32s-kit/build-probe/bs21_all_in_one.fwpkg
```

用途：decoy 改动后先用 probe 本地读回属性表验证呈现效果（handle+type+值），
无需消耗 dongle 测试轮次；最终再插官方 dongle 做端到端确认。

#### 已知 SDK 陷阱（probe 侧）

- `ssapc_find_structure_cb` / `ssapc_find_property_cbk` 返回的 UUID 是错的：
  总是 37BE（=0xBE33，描述符 UUID），不是真实的 UUID。SDK 解析器读错了偏移。
  绕过方法：在 `probe_dump_discovery_cfm` 里从原始 PDU 解析 UUID，查表替换。
- `ssapc_read_req` 签名是 `(client_id, conn_id, handle, type)`，不是结构体指针。
- SDK 拒绝 find type 2/4/5（REFERENCE_SERVICE/METHOD/EVENT）err=0x7，即使 UUID
  正确也不支持。核心发现（type 0/1/3）+ 读取属性值不受影响。
- 防掩耳铁律：probe 的 RX 原始 PDU（`RX len=N:` 行）= 数据真相；SDK 回调里的
  UUID/start_hdl 等 = 观察者侧值，可能不等于真相。双侧 diff 以原始 PDU 为准。

## 开发规范

### 防"掩耳盗铃"式修复（重要，适用所有代码修改）

观察者的解析/显示/返回值**不等于数据真相**——可能由观察者侧机制产生
（base UUID 组装、回填、缓存、固定长度等）。禁止"只让观察者显示变对"
的修复。本项目的惨痛案例（uuid len=2 假象掩盖 value=0000）见
`docs/ssap-uuid-false-fix.md`。

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
