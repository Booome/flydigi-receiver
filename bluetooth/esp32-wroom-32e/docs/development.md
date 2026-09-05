# ESP32-WROOM-32E 开发笔记（M9 / 蓝牙方向）

## 环境前提

- ESP-IDF v6.0.2 已通过 yay AUR 安装：`yay -S esp-idf`
- 工具链在 `~/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/`
- 激活：`source /opt/esp-idf/export.sh`（idempotent，可放 `.bashrc`）
- 验证：`idf.py --version` → `ESP-IDF v6.0.2`
- ESP32 在 ESP-IDF v6.0 仍受支持（`examples/get-started/hello_world/README.md` 的 Supported Targets 含 `ESP32`）

## 项目布局

- `apps/<app>/` — 每个 app 一个独立 ESP-IDF 项目（顶层 `CMakeLists.txt` + 源文件 + `sdkconfig.defaults`）
- `build/<app>/` — 编译产物，通过 `idf.py -B ../../build/<app>` 落到 board 顶层
- `tools/build.py` — `idf.py set-target && build` 包装；`--app <n>`、 `--clean`、 `--no-set-target`
- `tools/burn.py` — `idf.py flash` 包装；端口从顶层 `.env` 的 `BOARD_A_PORT` / `BOARD_B_PORT` 读
- `docs/` — 平台专属文档

## 串口抓取

与 SLE 共用顶层 `tools/capture_uart.py`。ESP32 DevKitC 板载 USB-UART 桥已把
DTR 接 EN，所以 ESP32 走 **DTR 复位**（不需 `uart-gpio` / ctrl pin）。

```bash
# 一次性抓（不复位）
python3 tools/capture_uart.py --board-a --duration 10 --odir /tmp --ts

# 抓 + DTR 复位（前提：.env 里有 BOARD_A_TYPE=esp32-wroom-32e）
python3 tools/capture_uart.py --board-a --rst-a --duration 14 --odir /tmp --ts
```

`.env` 里需要：
```
BOARD_A_PORT=/dev/serial/by-path/pci-...   # 串口路径（与 SLE 复用）
BOARD_A_TYPE=esp32-wroom-32e               # 选 DTR 复位机制
```

复位机制走 `BOARD_<X>_TYPE` 分派：
- `esp32-wroom-32e` —— DTR toggle（DevKitC 板载 USB-UART 桥）
- `ai-bs21-32s-kit` / `bearpi-pico-h3863` —— `uart-gpio` 脉冲（BS21/WS63 接控制板）

## 添加新 app

```bash
# 1. 从 ESP-IDF example 复制（不修改原 example）
cp -r /opt/esp-idf/examples/bluetooth/blufi bluetooth/esp32-wroom-32e/apps/blufi

# 2. 保留 example 自带的 main/ 子目录（ESP-IDF 自动发现为 main 组件）
# 3. 写 apps/blufi/sdkconfig.defaults（如需）
# 4. 构建 + 烧录
python3 tools/build.py --app blufi
python3 tools/burn.py --app blufi
```

## 添加共享 component（跨 app 复用代码）

```bash
# 例：BT 通用工具组件（被 hello_world / bt_inquiry / 后续 app 都依赖）
mkdir -p bluetooth/esp32-wroom-32e/components/bt_common/include
```

`components/<name>/` 下标准结构：

```
components/bt_common/
├── CMakeLists.txt       # idf_component_register(SRCS "bt_common.c" REQUIRES bt driver)
├── include/             # 公开头文件（其他组件 #include "bt_common.h" 用）
│   └── bt_common.h
├── bt_common.c
└── bt_common.h          # 私有头文件（仅本组件内）
```

ESP-IDF 自动发现 `components/<name>/`（`project.cmake:500`），无需在根 `CMakeLists.txt` 写 `EXTRA_COMPONENT_DIRS`。

其他组件 / app 通过 `idf_component_register(... REQUIRES bt_common)` 引用；头文件用 `#include "bt_common.h"`（公开头在 `include/` 子目录里，ESP-IDF 自动加进 include 路径）。

## 故障排查

| 症状 | 可能原因 | 处理 |
|---|---|---|
| `idf.py: command not found` | export.sh 未 source | `source /opt/esp-idf/export.sh` |
| 烧录卡在 `Connecting...` | 端口错 / 模块未上电 | 查 `.env` 的 `BOARD_A_PORT`；检查 DevKitC USB；`lsusb` 看 CP2102/CH340 |
| 烧录后串口无输出 | GPIO 不对 / 波特率不对 | DevKitC UART0 默认 GPIO1/3、115200；`idf.py monitor` 验证 |
| `Hard resetting via RTS pin` 后无 log | USB-UL 桥未触发 boot | 按 DevKitC 上的 EN 按钮手动复位；或确认 CP2102 DTR/RTS 接对 |
| `main` 组件找不到 | `main/CMakeLists.txt` 缺失或 `add_subdirectory(main)` 误删 | 恢复 `main/CMakeLists.txt`（含 `idf_component_register(SRCS ...)`） |
| `.env` 找不到 | worktree 创建时未带过来 | 从主仓库 `cp .env <worktree>/.env`；`capture_uart.py` 用 walk-up 自动找根 |

## 后续里程碑（不在 M9 范围）

- M10：BT inquiry（经典 BT 扫描，看到手柄）
- M11+：BluedR HID 主机连接手柄（`esp_hid_host`）
- HID 报告 TLV 化（复用现有 `formatter`）

## M10：BT 双模扫描（已完成）

详见：
- 设计：`docs/superpowers/specs/2026-09-05-bt-scan-design.md`
- 实施计划：`docs/superpowers/plans/2026-09-05-bt-scan.md`
- App：`apps/bt_scan/`（基于 `examples/bluetooth/esp_hid_host/` 精简）
- 验证记录：`apps/bt_scan/README.md` "验证结果" 节（含手柄 MAC / NAME / COD / RSSI）

### 实测关键发现

- 手柄八爪鱼5 在 BT 模式下走 **BR/EDR 经典蓝牙**，BLE 不广播（纠正 docs/controller-modes.md 之前"双模蓝牙"猜测——**手柄是单模 BR/EDR**，BP1Y303-D4 不是双模芯片）
- 手柄 BR/EDR 广播 name = `Xbox Wireless Controller`（不是"Flydigi Apex5"），是手柄固件把 HID-over-BR/EDR 映射成 Xbox 兼容身份
- ESP-IDF v6.0.2 的 `esp_hid_scan()` 自带 GAP debug print（`BT : ...` / `BLE: ...` 前缀），方便对比 esp_hid HID 过滤的 hit（`[bt_scan]` 行）vs 真实收到的所有广播
- esp_hid 的 HID 匹配只对 UUID 0x1812（BLE）+ 标准 BR/EDR HID UUID（0x1124）；不是 HID 设备的广播只出现在 GAP debug、不进 results 列表

### 复位机制（`capture_uart.py`）

ESP32 DevKitC 板载 USB-UART 桥已把 DTR 接 EN，所以 ESP32 走 **DTR 复位**
（不需 `uart-gpio` / ctrl pin）。

```bash
# 一次性抓（不复位）
python3 tools/capture_uart.py --board-a --duration 10 --odir /tmp --ts

# 抓 + DTR 复位（前提：.env 里有 BOARD_A_TYPE=esp32-wroom-32e）
python3 tools/capture_uart.py --board-a --rst-a --duration 14 --odir /tmp --ts
```

`.env` 里需要：
```
BOARD_A_PORT=/dev/serial/by-path/pci-...   # 串口路径（与 SLE 复用）
BOARD_A_TYPE=esp32-wroom-32e               # 选 DTR 复位机制
```

复位机制走 `BOARD_<X>_TYPE` 分派：
- `esp32-wroom-32e` —— DTR toggle（DevKitC 板载 USB-UART 桥）
- `ai-bs21-32s-kit` / `bearpi-pico-h3863` —— `uart-gpio` 脉冲（BS21/WS63 接控制板）
## M11：BT HID 主机连接 + 原始报告采集（已完成）

详见：
- 设计：`docs/superpowers/specs/2026-09-05-bt-hid-host-capture-design.md`
- 实施计划：`docs/superpowers/plans/2026-09-05-bt-hid-host-capture.md`
- App：`apps/default/`（**项目主 app**，从本里程碑起固定在 `default`，后续功能迭代都更新这里）
- 验证日志样例：`docs/sample-default-capture.log`

### 三层候选算法（spec §四）

```
层1 语义过滤:  只保留 gamepad-class BR/EDR (COD major=5 minor=2)
层2 EWMA 平滑:  smoothed = 0.3*新 + 0.7*旧  (alpha=0.3)
层3 迟滞 + 兜底: 平滑后差 >= 3dB 才换候选; 3s 稳定锁; 8s 强制连
```

常量：HYSTERESIS_DB=3, LOCK_WAIT_MS=3000, MAX_WAIT_MS=8000, EWMA alpha=0.3, CANDIDATE_GONE_ROUNDS=2。

### 工具默认值改动

- `tools/build.py` / `tools/burn.py`：`--app` 默认 `"default"`（原 `"hello_world"`）。
- 显式 `--app hello_world` / `--app bt_scan` 仍可指定。
- 调用方不传 `--app` 即烧/构建主功能 `default`。

### 输出格式

每行一条 HID 输入报告：
```
[hid] candidate: addr=b5:5d:e7:98:54:75 smoothed=-41.0
[hid] open: addr=b5:5d:e7:98:54:75 transport=BR_EDR
[hid] report: addr=b5:5d:e7:98:54:75 transport=BR_EDR len=15 data=0080ff800080ff8000000000000000
[hid] close: addr=b5:5d:e7:98:54:75 transport=BR_EDR status=0x13
```

### 关键陷阱（已踩）

**`esp_hid_scan()` 即使只用 BR/EDR 也会在 BLE 信号量上阻塞**——必须 `esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler)`。漏调 → 候选循环静默卡死，30s+ 看不到任何 `[hid] candidate` 输出。`main.c` 里 fix 见 commit `dbc385a`。

### 里程碑命名约定

不再用 `M<n>` 编号——换板时 Mn 容易乱。新里程碑按**工作内容命名**（`apps/hello_world/`、`apps/bt_scan/`、`apps/default/`）。spec/plan/分支名 = `<topic>-<action>` 形式。
