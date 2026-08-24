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
- SDK: 安信可 **Ai-BS21_SDK**（`~/.local/Ai-BS21_SDK`），只读引用模式（SDK 不修改源码）
- target: `bs21-n1100-rcu`（SLE-only，512KB flash）
- 编译：`cmake -S wireless/bs21 -B wireless/bs21/build && cmake --build wireless/bs21/build -j`（一次性前置 `wireless/bs21/scripts/setup-sdk.sh`）
- 开发环境搭建和路线图见 `docs/bs21-development.md`

#### 烧录与调试

串口/复位配置记录在项目根 `.env`（不入库，模板见 `.env.example`），统一用
`/dev/serial/by-path/` 稳定路径（ttyUSB 编号会漂移，勿硬编码）。复位 GPIO 由控制串口
（STM32）提供。相关脚本（`burn.py`、`bs21_connect.py`、`bs21_disconnect_test.py`）
自动从 `.env` 读取。

**自动烧录（推荐）**：
```bash
python3 wireless/bs21/tools/burn.py board_a   # 或 board_b
```
- 状态机：直接跑 ws63flash（pty 实时输出）→ 等 2s 判定 boot. 循环态；无则脉冲复位
  等 1s 判定正常态；仍无 = 卡死态（复位无效，只能手动拔插模块 USB 电源恢复）。

手动烧录（先发命令，等 "Waiting for device reset..." 后复位触发）：
```bash
ws63flash --flash <模块串口> wireless/bs21/build/<app>/bs21_all_in_one.fwpkg -b460800
# 另一终端，复位模块（<控制串口> 与 <引脚> 见 .env 的 *RST_PORT / *RST_PIN）：
uart-gpio pulse <控制串口> A <引脚> 0 2000
```

抓取从 reset 起的完整 log（模块无法靠拉低 reset 停止，会反复 reset 打印多轮）：
```bash
stty -F <模块串口> 115200 raw -echo
cat <模块串口> > /tmp/reset.log &
uart-gpio pulse /dev/ttyUSB5 A <引脚> 0 2000   # 复位，触发一轮启动 log
# 等几秒后 kill 掉 cat
```

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

提示音脚本：`wireless/bs21/tools/notify.sh`（播放 `notify_alarm.wav`，一段明显的
三连蜂鸣 + 收尾高音，三角波 vol=0.70 + 10ms 淡入淡出包络，无破音）。重新生成用
`notify_gen.py`。播放依赖 `paplay`（PulseAudio），无音频环境会报错退出。

调用示例：
```bash
bash wireless/bs21/tools/notify.sh
# 然后打印："请连接手柄并进入 2.4GHz SLE 配对模式"
```

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
cmake -S wireless/bs21 -B wireless/bs21/build-decoy -DBS21_APP=flydigi_decoy
cmake -S wireless/bs21 -B wireless/bs21/build-probe -DBS21_APP=sle_probe
# 烧录（显式指定 fwpkg）：
python3 wireless/bs21/tools/burn.py board_b wireless/bs21/build-decoy/bs21_all_in_one.fwpkg
python3 wireless/bs21/tools/burn.py board_a wireless/bs21/build-probe/bs21_all_in_one.fwpkg
```

用途：decoy 改动后先用 probe 本地读回属性表验证呈现效果（handle+type+值），
无需消耗 dongle 测试轮次；最终再插官方 dongle 做端到端确认。

## 开发规范

### C 代码格式

修改任何 `.c` 或 `.h` 文件后，**必须**使用 `clang-format` 格式化：

```bash
clang-format -i <修改的文件>
```

项目根目录已配置 `.clang-format`（LLVM 风格，4 空格缩进，100 列宽）。
格式化后再提交，确保代码风格一致。
