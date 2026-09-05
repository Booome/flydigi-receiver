# ESP32 蓝牙双模扫描 M10 设计

## 一、背景与目标

M9 完成 ESP32-WROOM-32E 开发环境搭建（`bluetooth/esp32-wroom-32e/` 完整 hello_world 编译/烧录/串口链路打通；详见 M9 spec/plan）。M10 迈出蓝牙协议研究第一步——**ESP32 主动扫描**周围蓝牙设备，验证能看到飞智八爪鱼5的**蓝牙模式**广播。

八爪鱼5 有三种物理连接：USB 有线 / 2.4GHz（星闪 SLE） / **蓝牙（BR/EDR）**。SLE 方向（`wireless/bearpi-pico-h3863/`）因 WS63↔BS2x 平台级不兼容已挂起；蓝牙方向独立。M10 的目标是**确认 ESP32 能看见手柄蓝牙模式**，为后续 M11+ 做 BR/EDR HID 主机连接（`esp_hid_host_open`）铺路。

**关键背景**（基于源码验证）：
- 飞智八爪鱼5 的蓝牙模式是**双模蓝牙**（同时支持 BR/EDR + BLE）：一部分走经典蓝牙（Xbox 风格 HID，UUID 0x1124），一部分走 BLE（GATT HID）
- ESP32-WROOM-32E（Xtensa LX6 原版）是 ESP-IDF v6.0.2 全家族**唯一**带 BR/EDR 的 SoC（S2/S3/C3/C5/C6/H2 全 BLE-only）—— 选型唯一
- ESP-IDF v6.0.2 提供 `examples/bluetooth/esp_hid_host/`（双模 HID 主机完整示例）+ 一等组件 `components/esp_hid/`，`esp_hid_scan()` 一行同时跑 BR/EDR inquiry + BLE scan
- 现有 SLE 项目的 `formatter` / UART 基建在 M10 **不**接入（M11+ 涉及多数据流时再考虑）；M10 输出走 `printf` 纯文本，便于人眼 + grep

**M10 范围**：仅双模扫描，**不**主动连接手柄，**不**涉及 SLE。Board_a 单板运行。

## 二、技术栈与约束

| 项 | 选型 | 说明 |
|---|---|---|
| 平台 | ESP32-WROOM-32E（ESP32-D0WD-V3 rev3.1） | 唯一带 BR/EDR 的 Espressif 芯片 |
| 框架 | ESP-IDF **v6.0.2**（AUR @ `/opt/esp-idf`） | BluedR 主机栈 |
| Bluetooth 栈 | BluedR（默认，非 NimBLE） | 双模必需 BluedR；NimBLE 仅 BLE |
| 核心 API | `esp_hid_gap_init(ESP_BT_MODE_BTDM)` + 循环 `esp_hid_scan(seconds, &n, &results)` | `esp_hid_scan` 一次同时跑 BR/EDR inquiry + BLE scan（`esp_hid_gap.c:1143-1190`） |
| 输出 | `printf` 纯文本，stdout | 一行一条，含 mode/addr/name/cod/rssi |
| 烧录 | `tools/burn.py --app bt_scan` 烧 board_a | 端口从 `.env` `BOARD_A_PORT` 读 |
| 抓 log | `tools/capture_uart.py --board-a` | 复用 SLE/BT 共享脚本（顶层 `tools/`） |

### 硬约束

- **非侵入式编译**：ESP-IDF `/opt/esp-idf` 只读；`apps/bt_scan/` 从 `/opt/esp-idf/examples/bluetooth/esp_hid_host/` `cp -r` 复制后**改我们的副本**，不动 ESP-IDF
- **多 app 支持**：每个 app 独立 ESP-IDF 项目（`apps/<app>/` + `build/<app>/`），沿用 M9 约定
- **Board 唯一**：仅 board_a 跑 M10；board_b 保持 hello_world（避免双板同时动作干扰）
- **不主动连**：M10 **仅扫描**，不调用 `esp_hid_host_open()` 等任何连接 API（避免污染手柄状态 / 未授权连报）
- **不引入 TLV formatter**：M10 输出纯文本；M11+ 涉及多数据流时再考虑复用 SLE 的 `formatter`
- **手柄端操作**：用户需手动把 Apex5 切到蓝牙模式（背面拨到中间，蓝色 LED 亮）+ 长按某键进入配对状态——M10 验证依赖此人工操作

## 三、项目布局

```
flydigi-receiver/
├── bluetooth/esp32-wroom-32e/
│   ├── apps/
│   │   ├── hello_world/              # M9 app（保留）
│   │   └── bt_scan/                  # 🆕 M10 app
│   │       ├── CMakeLists.txt        # project()
│   │       ├── main/
│   │       │   ├── CMakeLists.txt    # idf_component_register(SRCS "main.c" REQUIRES esp_hid bt nvs_flash)
│   │       │   └── main.c            # ~80 行：init esp_hid + loop esp_hid_scan + 打印
│   │       ├── sdkconfig.defaults    # 双模 + HID host 配置（复用 example 的）
│   │       └── README.md             # 用法 + 验证步骤
│   ├── build/                        # 编译产物
│   │   ├── hello_world/
│   │   └── bt_scan/                  # 🆕 M10 产物
│   ├── components/                   # 跨 app 共享（M10 暂空）
│   └── tools/{build,burn}.py         # 沿用 M9，新增 --app bt_scan 支持
└── ...
```

`tools/build.py` / `tools/burn.py` 在 M9 已支持 `--app <name>`（默认 hello_world）。M10 直接传 `--app bt_scan` 即可，**无需修改工具**。

## 四、App 设计：`apps/bt_scan/main.c`

### 核心逻辑（伪代码）

```c
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // 关键：ESP_BT_MODE_BTDM = 双模（BR/EDR + BLE）
    ESP_ERROR_CHECK(esp_hid_gap_init(ESP_BT_MODE_BTDM));
    ESP_LOGI(TAG, "ESP_HID_GAP initialized in dual mode (BR/EDR + BLE)");

    while (1) {
        esp_hid_scan_result_t *results = NULL;
        size_t num_results = 0;
        esp_err_t scan_ret = esp_hid_scan(SCAN_DURATION_SEC, &num_results, &results);
        if (scan_ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_hid_scan failed: %s", esp_err_to_name(scan_ret));
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        for (size_t i = 0; i < num_results; i++) {
            esp_hid_scan_result_t *r = &results[i];
            const char *mode_str = (r->transport == ESP_HID_TRANSPORT_BLE) ? "BLE" : "BR_EDR";
            const char *name = (r->name && r->name[0]) ? r->name : "";
            // 过滤掉自指（ESP32 自身有时候会出现在 BLE 扫描里）
            printf("[bt_scan] mode=%-5s addr=%02x:%02x:%02x:%02x:%02x:%02x "
                   "name=\"%s\" cod=0x%06x rssi=%d\n",
                   mode_str,
                   r->bda[0], r->bda[1], r->bda[2],
                   r->bda[3], r->bda[4], r->bda[5],
                   name, (unsigned)r->cod, r->rssi);
        }

        esp_hid_scan_results_free(results);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
```

### 从 example 的删减

| 保留 | 删 |
|---|---|
| `esp_hid_gap_init(ESP_BT_MODE_BTDM)` 双模初始化 | `esp_hid_host_open()` 自动连接最近一台设备 |
| `esp_hid_scan(SCAN_DURATION_SEC, ...)` 扫描循环 | `hidh_scan_result_t->open` 字段的连接判断 |
| `esp_hid_scan_results_free()` | `BTIF_STORAGE_CB` 持久化 + auto-reconnect 任务 |
| BR/EDR + BLE 双模 sdkconfig.defaults | NimBLE 相关 Kconfig |

### 关键 ESP-IDF API（已知 / 待实现时复核）

- `esp_hid_gap_init(uint8_t mode)` —— mode 取 `ESP_BT_MODE_BTDM`（双模）/`ESP_BT_MODE_CLASSIC_BT`（仅经典）/`ESP_BT_MODE_BLE`（仅 BLE）
- `esp_hid_scan(uint32_t seconds, size_t *num, esp_hid_scan_result_t **results)` —— 阻塞 N 秒，返回拼接好的 BR/EDR + BLE 结果链表
- `esp_hid_scan_result_t` 字段：`bda[6]` / `name`（可能为 NULL）/ `transport`（`ESP_HID_TRANSPORT_BLE` / `ESP_HID_TRANSPORT_CLASSIC_BT`）/ `cod` / `rssi`
- `esp_hid_scan_results_free(results)` —— 释放链表

## 五、`sdkconfig.defaults`

复用 `examples/bluetooth/esp_hid_host/sdkconfig.defaults` 的双模开关：

```
# Bluetooth dual-mode (BR/EDR + BLE) for HID host scanning
CONFIG_BT_ENABLED=y
CONFIG_BTDM_CTRL_MODE_BTDM=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_BT_CLASSIC_ENABLED=y
CONFIG_BT_BLE_ENABLED=y
CONFIG_BT_HID_ENABLED=y
CONFIG_BT_HID_HOST_ENABLED=y
CONFIG_BT_BLE_42_FEATURES_SUPPORTED=y
CONFIG_BT_GATTC_NOTIF_REG_MAX=16

# Single-app partition table (large) — esp_hid_host example default
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y

# Target chip (一致 with hello_world)
CONFIG_IDF_TARGET="esp32"
```

> **注意**：`CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE` 给 2MB flash 用了大 app 分区（1.7MB），BLE + BluedR + esp_hid 大约占 ~1.2MB；单 app 分区足够。Board_a 2MB flash 够用，board_b 4MB 当然也行。

## 六、输出格式

一行一条命中，便于 `grep` / 文本解析：

```
[bt_scan] mode=BR_EDR addr=AA:BB:CC:DD:EE:FF name="Flydigi Apex5" cod=0x002504 rssi=-55
[bt_scan] mode=BLE    addr=11:22:33:44:55:66 name="Xbox Wireless Controller" rssi=-67
```

字段含义：
- `mode`: `BR_EDR` 或 `BLE`
- `addr`: 6 字节 MAC（little-endian per 字节）
- `name`: 设备名（UTF-8，可能为空）
- `cod`: Class of Device（24-bit hex，仅 BR/EDR 设备有；BLE 通常 0）
- `rssi`: 信号强度 dBm

每轮扫描（默认 5s）结束一次性打印本轮所有结果。

## 七、复用现有工具

M9 已有的工具链**直接复用**，M10 不改 tools：

```bash
# 构建
source /opt/esp-idf/export.sh
cd bluetooth/esp32-wroom-32e
python3 tools/build.py --app bt_scan              # build/hello_world/ 旁边产出 build/bt_scan/

# 烧录 board_a（默认 BOARD_A_PORT）
python3 tools/burn.py --app bt_scan               # 覆盖之前的 hello_world

# 抓 log
python3 ../../tools/capture_uart.py --board-a --duration 60 --odir /tmp --ts
```

`tools/build.py` / `tools/burn.py` 在 M9 已是 `--app <name>` 通用结构，M10 无需改动。

## 八、范围外（明确不做）

- ❌ 主动连接手柄（`esp_hid_host_open`）—— M11+
- ❌ HID 报告 TLV 化 / 复用 `formatter` —— M11+
- ❌ NimBLE / BLE-only 路径 —— M10 双模走 BluedR
- ❌ 扫描过滤（COD class / 设备名前缀 / RSSI 阈值）—— M10 全量打印
- ❌ CLI / AT 交互 —— M10 持续扫描、无交互
- ❌ Board_b 上跑 M10 —— M10 单板验证
- ❌ 扫描结果落 flash / NVS 持久化 —— M10 仅 stdout

## 九、文档同步

| 文件 | 改动 |
|---|---|
| `bluetooth/esp32-wroom-32e/apps/bt_scan/README.md` | 新增：用法、构建命令、验证步骤 |
| `bluetooth/esp32-wroom-32e/docs/development.md` | 追加 M10 节：双模扫描、esp_hid 简介、扫手柄经验 |
| `AGENTS.md` | "蓝牙方向"节追加 M10 进展 |
| `README.md` | "开发状态"更新：M10 验证完成 → "蓝牙模式可见" |
| `docs/superpowers/plans/2026-09-05-bt-scan.md` | 实施计划（本 spec 后续产出） |

## 十、验证里程碑

| # | 步骤 | 通过标准 |
|---|---|---|
| 1 | `python3 tools/build.py --app bt_scan` | 编译通过；`build/bt_scan/bt_scan.bin` 产出 |
| 2 | `python3 tools/burn.py --app bt_scan` | board_a 烧录成功；"Hash of data verified ... Done" |
| 3 | `capture_uart.py --board-a --duration 15` | 看到 `[bt_scan]` 启动 banner + 至少 1 轮扫描输出 |
| 4 | **手柄验证**：把 Apex5 切到 BT 模式（蓝色 LED） | ESP32 输出**同时**出现 BR/EDR 和 BLE 命中——BR/EDR 侧 name `Flydigi Apex5`（实测名以 `controller-modes.md` 为准）/ BLE 侧 name `Xbox Wireless Controller` |
| 5 | **对照验证**：另一台手机/电脑开着蓝牙 | ESP32 输出能看到这台对照设备的 BR/EDR 或 BLE 广播（任一即可），证明双模扫描鲁棒 |
| 6 | **退出验证**：关掉对照蓝牙设备 + 手柄 | ESP32 输出回到无命中 / 只看到 ESP32 自身的零星项 |

任一失败 → 按 `apps/bt_scan/README.md` 排障段处理。

## 十一、风险与决策

- **手柄需人工切换 BT 模式**：M10 不是纯软件测试，需用户在 capture 前拨杆 + 配对键。这是已知约束，验证步骤里明确写出。
- **双模 sdkconfig 缺一项 → 单模运行**：如果 `CONFIG_BT_CLASSIC_ENABLED` 或 `CONFIG_BT_BLE_ENABLED` 漏配，只会扫到一侧。Mitigation：`sdkconfig.defaults` 完整复用 example 配置，构建后用 `idf.py menuconfig` 视觉确认（或 grep `build/bt_scan/sdkconfig`）。
- **BR/EDR inquiry 5s 不一定每轮扫到所有设备**：默认 48-slot inquiry window + interval，5s 一轮通常能覆盖半径内大部分可见设备。如果验证 4 偶尔缺命中，可调 `SCAN_DURATION_SEC=10`。
- **board_a flash 是 2MB**：单 app 分区（`SINGLE_APP_LARGE`）给 1.7MB app；BLE + BluedR + esp_hid 估 ~1.2MB。**够用**。验证步骤 1 产出 `.bin` 即可确认实际大小；若超 1.7MB，临时改回 `SINGLE_APP`（1MB app）或换 board_b（4MB）。
- **ESP-IDF v6.0.2 与 ESP32 原版**：ESP32 仍在受支持目标（`examples/bluetooth/esp_hid_host/README.md` Supported Targets 含 `ESP32`）。BluedR 在原版 ESP32 上是 maintenance mode，但扫描功能稳定可用。
- **过滤 ESP32 自身**：BLE 扫描有时会扫到 ESP32 自己的广播（如被另一台 ESP32 配置了 peripheral），按需过滤（`addr != esp_bt_dev_get_address()`）；M10 暂不过滤，全量打印让用户自己判断。

## 十二、本里程碑的"做完"

- `bluetooth/esp32-wroom-32e/apps/bt_scan/` 可 build + flash + 抓 log
- board_a 烧 bt_scan 后，手柄在 BT 模式下**双侧都能看到**（BR/EDR + BLE 各至少一条命中）
- `AGENTS.md` / `README.md` / `apps/bt_scan/README.md` / `docs/development.md` 同步更新
- 提交到 `m10-bt-scan` 分支，未合并
- 不破坏 M9 hello_world app（同时保留）