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

串口/复位配置记录在 `wireless/bs21/tools/burn_config.yaml`（不入库，模板见同目录
`burn_config.yaml.example`），统一用 `/dev/serial/by-path/` 稳定路径（ttyUSB 编号会漂移，
勿硬编码）。复位 GPIO 由控制串口（STM32）提供。

**自动烧录（推荐）**：
```bash
python3 wireless/bs21/tools/burn.py board_a   # 或 board_b
```
- 状态机：直接跑 ws63flash（pty 实时输出）→ 等 2s 判定 boot. 循环态；无则脉冲复位
  等 1s 判定正常态；仍无 = 卡死态（复位无效，只能手动拔插模块 USB 电源恢复）。

手动烧录（先发命令，等 "Waiting for device reset..." 后复位触发）：
```bash
ws63flash --flash <模块串口> wireless/bs21/build/<app>/bs21_all_in_one.fwpkg -b460800
# 另一终端，复位模块（<控制串口> 与 <引脚> 见 burn_config.yaml 的 ctrl_port / reset_pin）：
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

## 手柄硬件信息

- 型号：飞智八爪鱼5 (Flydigi Apex 5)
- FCC ID：2AORE-K5
- 2.4GHz 芯片：P352903N1（星闪 SLE 1.0，飞智定制编号）
- 蓝牙芯片：BP1Y303-D4（BR/EDR）
- USB VID/PID：0x37D7 / 0x2501
