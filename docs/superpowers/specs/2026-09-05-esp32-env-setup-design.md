# ESP32-WROOM-32E 开发环境搭建（M9 / 蓝牙方向第一里程碑）

## 一、背景与目标

飞智八爪鱼5（Flydigi Apex 5）有三种物理连接：USB 有线、2.4GHz（星闪 SLE）、蓝牙（BR/EDR HID）。当前 SLE 方向（`wireless/bearpi-pico-h3863/`）已确认硬件层不通（WS63↔BS2x 广播平台级不兼容，详 `docs/history.md` 与 m3 提交链），蓝牙方向接续。

蓝牙模式独立于 SLE：
- 协议：BR/EDR（经典蓝牙），HID 设备识别为 "Xbox Wireless Controller"（UUID 0x1124）；非 BLE GATT
- 蓝牙芯片：BP1Y303-D4（已在 `docs/controller-modes.md` 注明）

**本里程碑目标**：用 ESP32-WROOM-32E 调通开发环境，验证 hello_world 能编译 + 烧录 + 串口输出。**不**涉及手柄或任何蓝牙协议；后续 M10/M3 起再做 BT inquiry、HID 主机连接。

## 二、技术栈与约束

| 项 | 选型 | 说明 |
|---|---|---|
| 平台 | ESP32-WROOM-32E（ESP32-D0WD-V3，Xtensa LX6 双核 240MHz，rev3.1） | 已采购到货 |
| 框架 | ESP-IDF **v6.0.2**（yay AUR `esp-idf` 包） | 唯一支持 BR/EDR 主机（BluedR）的选项；Arduino 的 BT 仅 SPP，不支持 HID 主机 |
| ESP-IDF 位置 | `/opt/esp-idf`（AUR 管理，**不**复制到 `~/workspace/`） | 工具链在 `~/.espressif/tools/xtensa-esp-elf/...`；ESP32 仍在 v6.0 受支持目标之列（`examples/get-started/hello_world/README.md` 的 `Supported Targets` 包含 `ESP32`） |
| 工具链调用 | `source /opt/esp-idf/export.sh` 后 `idf.py` 直接可用 | AUR 包自带 export.sh；不写进项目 |
| 烧录 | `idf.py -p <port> flash`（带 esptool.py） | 通过 USB-UART 串口 |
| 串口路径 | `/dev/serial/by-path/pci-...port0` | 沿用项目统一规则；ttyUSB 漂移，禁硬编码 |
| 复位控制 | `uart-gpio`（同 SLE 共用 ctrl 板同一组 pin） | 用户确认 ESP32 DevKitC 同样接到这条复位通路；保留与 SLE 一致的物理配置 |
| 烧录/串口工具 | 顶层 `tools/capture_uart.py`（原 `wireless/tools/`，上移）+ ESP32 专用 `bluetooth/esp32-wroom-32e/tools/build.py` 与 `burn.py` | 见第四节 |

### 硬约束

- **wireless/ 是 SLE 专用目录**，蓝牙新建独立顶级目录 `bluetooth/`，与 `wireless/` 平级
- **非侵入式编译**：ESP-IDF 全程只读；不修改 `/opt/esp-idf/examples/...`；例子需 `cp -r` 进项目后再改
- **编译产物**落到 `bluetooth/esp32-wroom-32e/build/<app>/`（与 `apps/<app>/` 平列），不在 app 子目录
- **多 app 支持**：未来会加 `bt_inquiry`、`bt_hid_host` 等，每个 app 一个独立 ESP-IDF 项目

## 三、项目布局

```
flydigi-receiver/
├── AGENTS.md
├── README.md
├── .env                               # 顶层共享：SLE + BT 串口 + ctrl pin（已存在，扩展 ESP32 键）
├── tools/                             # 跨平台共享
│   ├── notify.sh                      # 提示音（已存在）
│   └── capture_uart.py                # 原 wireless/tools/capture_uart.py，上移；CLI 不变
├── wireless/                          # SLE 工程专用（不动）
│   ├── ai-bs21-32s-kit/
│   ├── bearpi-pico-h3863/
│   └── tools/
│       └── burn.py                    # SLE 专用 ws63flash
└── bluetooth/                         # 蓝牙工程专用（新顶级目录）
    └── esp32-wroom-32e/
        ├── README.md                  # 平台概览
        ├── docs/
        │   └── development.md         # ESP-IDF 位置、构建/烧录/串口流程
        ├── apps/                      # 每个 app 一个独立 ESP-IDF 项目（可写）
        │   └── hello_world/
        │       ├── CMakeLists.txt      # 顶层 ESP-IDF project
        │       ├── main.c            # 源码直接放 app 根（无 main/ 子目录）
        │       ├── sdkconfig.defaults # CONFIG_IDF_TARGET="esp32" 等
        │       └── README.md
        ├── build/                     # 编译产物（不入库）
        │   └── hello_world/
        └── tools/
            ├── build.py               # idf.py -C apps/<app> -B ../../build/<app> build
            └── burn.py                # idf.py -C apps/<app> -B ../../build/<app> -p <port> flash
```

## 四、共享工具调整：`capture_uart.py` 上移

**操作**：将 `wireless/tools/capture_uart.py` **移动到顶层 `tools/capture_uart.py`**（与 `tools/notify.sh` 同级）。

**关键**：**不**改 CLI 接口、不增键。理由：
- ESP32 DevKitC 接入位置与 board_a/board_b 同位置（同 `BOARD_A_PORT` / `BOARD_B_PORT`，同 ctrl 板 reset pin）
- 现有 `--board-a/--board-b/--rst-a/--rst-b` 标志对 SLE 与 ESP32 **通用**——抓哪路串口、要不要脉冲复位都走同一套
- 顶层 `.env` **不动**（现有 `BOARD_A_PORT`、`BOARD_B_PORT`、`CTRL_PIN` 已覆盖）

移动后行为完全一致：连串口、可选复位脉冲、落盘、时间戳、`Ctrl+C` 优雅保存。

`wireless/tools/burn.py`（SLE 专用 ws63flash）**保留**不动。**`bluetooth/esp32-wroom-32e/tools/burn.py`** 用 `idf.py flash`，端口从 `.env` 的 `BOARD_A_PORT` / `BOARD_B_PORT` 读（CLI 选哪个 board）。

## 五、第一个 app：hello_world

**移植步骤**（首次）：
1. `cp -r /opt/esp-idf/examples/get-started/hello_world bluetooth/esp32-wroom-32e/apps/hello_world`
2. 进入 `apps/hello_world/`，顶层 `CMakeLists.txt` 内 `PROJECT_NAME` 保持 `hello_world`（无歧义）
3. 将 `main/hello_world_main.c` 移到 app 根并**改名为 `main.c`**（app 内不再带 app 名前缀）：`mv apps/hello_world/main/hello_world_main.c apps/hello_world/main.c`，同步更新 `main/CMakeLists.txt` 改为引用 `../main.c`，或直接将 `main/` 内容合并到 app 根 `CMakeLists.txt` 的 `idf_component_register(SRCS "main.c")` 中
4. 删除空的 `main/` 目录与 `main/CMakeLists.txt`

最终 `apps/hello_world/` 内容：
```
apps/hello_world/
├── CMakeLists.txt       # 顶层（PROJECT_NAME hello_world，idf_component_register(SRCS "main.c")）
├── main.c               # 从原 example 的 main/hello_world_main.c 改名而来
├── sdkconfig.defaults   # 新增，固定 CONFIG_IDF_TARGET="esp32"，避免每次 set-target
└── README.md            # 简述：用途、依赖 ESP-IDF v6.0.2、构建命令
```

**构建**（`tools/build.py hello_world`）：
```bash
idf.py -C apps/hello_world -B ../../build/hello_world set-target esp32
idf.py -C apps/hello_world -B ../../build/hello_world build
```

`-B ../../build/hello_world` 把 ESP-IDF 默认的 `<app>/build/` 重定向到 `bluetooth/esp32-wroom-32e/build/hello_world/`（顶层 build 下），保证非侵入式 + build/ 与 apps/ 平列。

**烧录**（`tools/burn.py hello_world`）：
```bash
idf.py -C apps/hello_world -B ../../build/hello_world -p "$BOARD_A_PORT" flash
```
端口从顶层 `.env` 读（`BOARD_A_PORT` 或 `BOARD_B_PORT`，由 `burn.py` CLI 参数选）；ESP32 复用 SLE 的同名键，不增 `.env` 项。

## 六、范围外（明确不做）

- BT inquiry / 扫描手柄（→ M10）
- BR/EDR HID 主机连接手柄（BluedR `esp_hid_host`）→ M3+
- HID 报告 TLV 化 / 复用 `formatter` → M3+
- bluepad32（`~/workspace/bluepad32` 存在；仅作参考资料，不复用其 BT HID 主机栈）
- USB HID / Wi-Fi 直连 PC 等 PC 链路——本次仅串口验证；M3+ 再定
- ESP32-S2/S3/C3 等其他 ESP32 系列——本次仅 ESP32-WROOM-32E

## 七、文档同步

| 文件 | 改动 |
|---|---|
| `bluetooth/esp32-wroom-32e/README.md` | 新增：平台概览、ESP-IDF 位置、构建/烧录/串口命令 |
| `bluetooth/esp32-wroom-32e/docs/development.md` | 新增：环境细节（export.sh、`-B` 路径）、`.env` 键、常见故障 |
| `apps/hello_world/README.md` | 新增：app 用途、构建命令 |
| `AGENTS.md` | "项目状态"增 ESP32/BT 方向；"共享工具"段更新：`capture_uart.py` 路径改顶层，`burn.py` 仍 `wireless/tools/`，新增 ESP32 专用 `bluetooth/esp32-wroom-32e/tools/{build,burn}.py` |
| 顶层 `README.md` | 项目结构图加 `bluetooth/`；硬件表加 ESP32-WROOM-32E |
| `tools/capture_uart.py` | 从 `wireless/tools/` 移到顶层；CLI 不变 |
| `wireless/tools/` | 删除 `capture_uart.py`（移动到顶层后） |
| `.env` / `.env.example` | **不增键**；ESP32 复用 `BOARD_A_PORT` / `BOARD_B_PORT` / `CTRL_PIN` |

## 八、验证里程碑

| # | 步骤 | 通过标准 |
|---|---|---|
| 1 | `source /opt/esp-idf/export.sh && which idf.py` | 存在；`idf.py --version` 输出 `ESP-IDF v6.0.2` |
| 2 | `cd bluetooth/esp32-wroom-32e && python3 tools/build.py`（默认 hello_world） | `build/hello_world/` 下生成 `hello_world.bin`；零 error |
| 3 | `python3 tools/burn.py`（默认 hello_world） | DevKitC 上电、`idf.py -p "$BOARD_A_PORT" flash` 报 `Hash of data verified. Leaving... Hard resetting via RTS pin...` |
| 4 | `python3 ../../tools/capture_uart.py --board-a --duration 10 --odir /tmp --ts` | 看到 `Hello world!` + ESP32 启动日志（含 `rst:0x1 (POWERON)` 等） |

任一失败：按 `docs/development.md` 排障段处理；不阻塞其他模块。

## 九、风险与决策

- **ESP-IDF v6.0 与 ESP32（原始）支持**：v6.0 release notes 中 ESP32 仍在 `Supported Targets`（已确认），但 v6.x 主推 S2/S3/C3/C6 等。**决策**：用 v6.0.2；若未来 v6.x 移除 ESP32，降级 v5.4 LTS（v5.4 仍支持 ESP32）。
- **ESP32-WROOM-32E 是 BR/EDR 主机的唯一对口选择**：Espressif 家族中**只有原版 ESP32（Xtensa LX6）带 BT4.2 BR/EDR**。S2/S3/C3/C5/C6/H2/C61/E22 全部 BLE-only（E22 是 ESP-Hosted 共处理器，非独立 SoC）。原版 ESP32 在新设计中不被推荐（BluedR 维护模式，主推 S3/C6 等），但 BR/EDR 主机场景下**没替代**——本项目仅原型/研究，v6.0.2 仍完整支持，无影响。
- **AUR 包升级破坏环境**：AUR `esp-idf` 大版本升级可能改 `/opt/esp-idf` 路径或 Python 依赖。**决策**：版本号钉在 v6.0.2；升级前冻结。
- **`-B ../../build/<app>` 相对路径**：ESP-IDF 支持自定义 build 目录，但 `-B` 解析依赖 `pwd`。`tools/build.py` 必须 `cd apps/<app>` 后再调 `idf.py -B ../../build/<app>`。
- **DevKitC USB-UART 驱动**：常见 CP2102/CH340。如系统无驱动（极少，Linux 内核通常含），需装对应驱动。**决策**：验证 step 3 失败时再排查。
- **`capture_uart.py` ESP32 复位**：用户确认 ESP32 同样接到 ctrl 板的同一组 pin。ESP-IDF 的 `idf.py flash` 已通过 DTR/RTS 自动复位，**通常**不需要 `uart-gpio` 脉冲；但保留 `--rst-esp32` 通道以备非常规场景。

## 十、本里程碑的"做完"

- `bluetooth/esp32-wroom-32e/` 存在，`README.md` + `docs/development.md` 完整
- `apps/hello_world/` 可 `build` + `flash` + 串口看到输出
- 顶层 `tools/capture_uart.py` 上移，CLI 不变（ESP32 复用 `--board-a/--board-b/--rst-a/--rst-b`）
- `wireless/tools/` 删 `capture_uart.py` 后 SLE 路径不破
- `.env` / `.env.example` 增 ESP32 键
- AGENTS.md + 顶层 README 更新
- 提交到 `m9-esp32-env` 分支，未合并