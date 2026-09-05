# BT HID 主机连接 + 原始报告采集设计

## 一、背景与目标

项目蓝牙方向前两步完成：
- **esp32 env setup**：ESP-IDF v6.0.2 + BluedR 环境就绪（`bluetooth/esp32-wroom-32e/apps/hello_world/` 编译/烧录/串口全链路打通）。
- **bt_scan**：BR/EDR + BLE 双模扫描抓到飞智八爪鱼5 蓝牙模式广播（MAC `b5:5d:e7:98:54:75`，NAME `Xbox Wireless Controller`，COD major=PERIPHERAL minor=2，UUID 0x1124，纯 BR/EDR 不广播 BLE）。

下一步把"被动听"变成"主动搭话"：**ESP32 作为 BR/EDR HID 主机连接手柄，接收 HID 输入报告，原始字节 hex 打印到串口**。报告解析（按键/摇杆/扳机/震动）放到后续迭代，本次只做到"原始报告采集"。

**项目结构新约定**：从本里程碑起，主功能 app 固定在 **`apps/default/`**。`hello_world` 保留为环境验证基线（**不再更新**）。后续所有功能迭代更新 `default`，**不新建 app**。`build.py` / `burn.py` 不指定 `--app` 时默认 `default`。

**范围外**（后续更新 `apps/default/`）：
- HID 报告反格式（按键/摇杆/扳机解析）
- HID Output 报告（主动给手柄发震动）
- 多设备并发连接
- TLV formatter（数据流稳定后再考虑）

## 二、技术栈与约束

| 项 | 选型 | 说明 |
|---|---|---|
| 平台 | ESP32-WROOM-32E (board_a) | 同 M10；`BOARD_A_TYPE=esp32-wroom-32e` 走 DTR 复位 |
| 框架 | ESP-IDF v6.0.2（`/opt/esp-idf`，只读） | BluedR；esp_hid 主机栈 |
| 核心 API | `esp_hid_gap_init(ESP_BT_MODE_BTDM)` + 循环 `esp_hid_scan` + `esp_hid_host_open` + HID input callback | 沿用 `examples/bluetooth/esp_hid_host` 的 esp_hid API |
| 候选算法 | **持续扫 + 最强稳定 3s 窗口**：扫描期内有新更强 HID 候选 → 替换 + 重置计时；3s 内无变更 → 连接 | 详见 §四 |
| 输出 | 文本一行一条：`[hid] report: addr=...:... transport=BR_EDR len=N data=HEX...` | 与 M10 bt_scan 输出风格一致，便于后续解析 |
| 烧录 | `tools/burn.py --app default` 烧 board_a（默认走 BOARD_A_PORT） | |
| 抓 log | `tools/capture_uart.py --board-a --rst-a` | DTR 复位（同 M10） |

### 硬约束

- **不动 `apps/hello_world/`** —— 它是 M9 环境基线，保留为 sanity-check 用
- **`apps/default/` 是项目主 app 唯一归属** —— 后续功能迭代都更新这里
- **`build.py` / `burn.py` 默认 `app=default`** —— 调用方不传 `--app` 时自动构建/烧主功能
- **非侵入式** —— ESP-IDF `/opt/esp-idf` 只读；从 `examples/bluetooth/esp_hid_host/` cp -r 进 `apps/default/`，改副本
- **不主动连指定 MAC** —— 用候选算法 + 3s 稳定窗口（用户偏好），不硬编码 MAC；新设备/手柄切换自动适配
- **不解析 HID 报告** —— 本里程碑只采集原始字节；解析放后续 `default` 迭代
- **不复用 SLE formatter** —— 单数据流文本足够；TLV 暂不引入

## 三、项目布局

```
flydigi-receiver/
├── bluetooth/esp32-wroom-32e/
│   ├── apps/
│   │   ├── hello_world/   # 🟡 不动（M9 环境基线）
│   │   ├── bt_scan/       # 🟡 不动（M10 双模扫描工具）
│   │   └── default/       # 🆕 本里程碑 + 后续所有功能的主 app
│   │       ├── CMakeLists.txt                # project(default)
│   │       ├── main/
│   │       │   ├── CMakeLists.txt            # idf_component_register(SRCS "main.c" "esp_hid_gap.c" ...)
│   │       │   ├── esp_hid_gap.c/.h          # 从 esp_hid_host 拷贝（提供 esp_hid_scan + esp_hid_host_open）
│   │       │   └── main.c                    # 候选算法 + 连接 + hex 打印
│   │       ├── sdkconfig.defaults            # 双模 + HID host + CONFIG_IDF_TARGET="esp32"
│   │       └── README.md                     # 用法 + 验证步骤 + 已知限制
│   ├── build/             # build/default/ 编译产物（不入库）
│   └── tools/
│       ├── build.py       # default="default"（本里程碑改）
│       └── burn.py        # default="default"（本里程碑改）
├── docs/
│   ├── superpowers/specs/2026-09-05-bt-hid-host-capture-design.md   # 本文件
│   └── superpowers/plans/2026-09-05-bt-hid-host-capture.md        # 实施计划（后续产出）
└── ...
```

## 四、候选连接算法

```
init: esp_hid_gap_init(ESP_BT_MODE_BTDM)
      nvs_flash_init

state:
  candidate = {addr, transport, rssi} | NULL
  candidate_set_at = monotonic_ms()

loop (forever):
  if candidate == NULL OR now - candidate_set_at < 3000:
      # 还在候选阶段 / 倒计时阶段：持续扫
      esp_hid_scan(5s, &n, &results)
      best = pick_strongest(results)  # 选 RSSI 最大、BR_EDR 优先
      if best:
          if candidate == NULL OR best->rssi > candidate->rssi:
              # 任何新候选（或首次见到）→ 替换 + 重置计时
              candidate = best
              candidate_set_at = now
              log("[hid] candidate: ... rssi=...")
      esp_hid_scan_results_free(results)
  else:
      # 候选稳定 3s+ → 连接
      esp_hid_host_open(candidate->transport, candidate->bda)
      log("[hid] open: ...")
      break

# HID input callback（注册到 esp_hid）：
void on_hid_input(addr, transport, data, len):
    printf("[hid] report: addr=%02x:%02x:...:%02x transport=%s len=%d data=",
           ...);
    for (int i = 0; i < len; i++) printf("%02x", data[i]);
    printf("\n");

# disconnect callback：
void on_hid_close(addr, transport, reason):
    log("[hid] close: ... reason=0x%x", reason);
    candidate = NULL; candidate_set_at = 0;  # 清状态，回候选阶段
    esp_hid_host_open...  # 重新进入候选循环
```

**"明显更强" 阈值**：简化为"任何新 HID 候选都替换 + 重置计时"。原因：
- 候选阶段重置代价低（最多等 3s），新设备竞争/手柄切换场景下"明显更强"难精确量化
- 简化实现 + 行为可预测

**为何 3s**：用户在 brainstorming 阶段指定。手柄进入配对状态通常持续 ~30s-几分钟；3s 等待足够避开瞬态噪音（如路由器、其他 BT 设备的广播脉冲），又不会让用户久等。

## 五、`sdkconfig.defaults`

沿用 `apps/bt_scan/` 的双模配置：
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
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y

# Pin target chip
CONFIG_IDF_TARGET="esp32"
```

## 六、输出格式

每行一条：
```
[hid] candidate: addr=b5:5d:e7:98:54:75 transport=BR_EDR rssi=-50
[hid] candidate: addr=b5:5d:e7:98:54:75 transport=BR_EDR rssi=-48  (新更强候选，重置计时)
[hid] open: addr=b5:5d:e7:98:54:75 transport=BR_EDR
[hid] report: addr=b5:5d:e7:98:54:75 transport=BR_EDR len=15 data=80cc057f820102030405060708090a
[hid] report: addr=b5:5d:e7:98:54:75 transport=BR_EDR len=15 data=80cc057f820102030405060708090b
[hid] close: addr=b5:5d:e7:98:54:75 transport=BR_EDR reason=0x13
```

字段含义：
- `[hid] candidate:` —— 候选阶段，每 5s 一次
- `[hid] open:` / `[hid] close:` —— HID 链路建立/断开
- `[hid] report:` —— 每条输入报告（用户按键/摇杆变化触发）
- `transport:` ∈ `BR_EDR` / `BLE`
- `rssi:` dBm（候选阶段才有）
- `len:` 字节数
- `data:` hex 字符串（无空格，便于 grep / 后续解析）

## 七、复用现有工具（改动点）

M9/M10 已有的工具**直接复用**，但**调整默认值**：

| 工具 | 当前默认 app | 新默认 app | 改动 |
|---|---|---|---|
| `tools/build.py` | `hello_world` | `default` | `--app` 参数 default 值 |
| `tools/burn.py`  | `hello_world` | `default` | `--app` 参数 default 值 |

**不动的**：`.env` 键（BOARD_A_PORT/B_TYPE/CTRL_PIN 等，M10 已设置）、`tools/capture_uart.py`（M10 已带 BOARD_*_TYPE 分派）。

调用方无需指定 `--app`：
```bash
# 默认构建 default
python3 bluetooth/esp32-wroom-32e/tools/build.py
# 默认烧 default（覆盖 board_a 上的 bt_scan/hello_world）
python3 bluetooth/esp32-wroom-32e/tools/burn.py
# 抓 log（DTR 复位 + 持续看 60s）
python3 tools/capture_uart.py --board-a --rst-a --duration 60 --odir /tmp --ts
```

显式指定其他 app 仍可用（保留兼容性）：
```bash
python3 bluetooth/esp32-wroom-32e/tools/build.py --app hello_world
python3 bluetooth/esp32-wroom-32e/tools/build.py --app bt_scan
```

## 八、范围外（明确不做）

- ❌ HID 报告反格式 / 按键/扳机/摇杆解析 —— 后续 `default` 迭代
- ❌ HID Output（发震动）—— 后续 `default` 迭代
- ❌ TLV formatter —— 单数据流文本足够
- ❌ 多个 HID 设备并发连接
- ❌ 主动连接指定 MAC —— 用候选算法
- ❌ NimBLE —— 走 BluedR
- ❌ 改 `apps/hello_world/` —— M9 环境基线冻结
- ❌ 改 `apps/bt_scan/` —— M10 双模扫描工具冻结
- ❌ 新建第三个 app —— 默认 app 即归宿

## 九、文档同步

| 文件 | 改动 |
|---|---|
| `bluetooth/esp32-wroom-32e/apps/default/README.md` | 新增：候选算法说明、用法、验证步骤、范围外 |
| `bluetooth/esp32-wroom-32e/docs/development.md` | 追加"主 app 在 default"约定 + `build.py/burn.py` 默认值改动说明 |
| `AGENTS.md` | "蓝牙方向"节追加本里程碑 + 工具默认值改动 + 主 app 归属约定 |
| `README.md` | 项目结构图：突出 `apps/default/` 主 app 位置；硬件表/开发状态同步 |
| `tools/build.py` / `tools/burn.py` | `--app` 参数 default 改 `"default"` |
| `.gitignore` | 已有的 `bluetooth/*/build/` 覆盖 `default/` 产物 |

## 十、验证里程碑

| # | 步骤 | 通过标准 |
|---|---|---|
| 1 | `python3 bluetooth/esp32-wroom-32e/tools/build.py`（默认无参）| 编译通过；`build/default/default.bin` 产出 |
| 2 | `python3 bluetooth/esp32-wroom-32e/tools/burn.py`（默认无参）| board_a 烧录成功；"Hash of data verified ... Done"（DTR 自动复位）|
| 3 | `capture_uart.py --board-a --rst-a --duration 10` | 看到启动 + 候选阶段 `[hid] candidate: ...` 日志 |
| 4 | 手柄切 BT 模式 + 进配对 → ESP32 在 3s 内输出 `[hid] open: ...` |  |
| 5 | **按手柄按键** → 串口看到 `[hid] report: ... data=...` 持续输出（不同按键 → 不同 hex 模式） |  |
| 6 | 长时间观察（60s）→ HID 报告持续、稳定，无异常断开 | `[hid] close:` 不应出现（除非手柄关） |
| 7 | **手动关手柄** → ESP32 输出 `[hid] close: ...` + 重新进入候选阶段 → 等手柄再次开机 → 重新连接 | 验证 disconnect 重连逻辑 |

任一失败：按 `apps/default/README.md` 排障段处理。

## 十一、风险与决策

- **手柄需人工切 BT 模式** —— 同 M10，需用户操作。
- **候选算法的 3s 窗口** —— 用户指定。倒计时期间手柄需保持可见；如果中途手柄断电、离开范围，候选消失 → 重置。
- **断开重连**：HID close event 后清 candidate，回到候选阶段。M10 文档已确认 `esp_hid_host` 支持此模式。
- **BLE HID（如果手柄同时广播）**：候选算法已 BR_EDR 优先（`pick_strongest` 实现里 BR_EDR tiebreak）；但 M10 实测手柄不广播 BLE，所以实际只会走 BR/EDR 分支。
- **BLE HID 的 host API** 与 BR/EDR 略不同（esp_hid_host_open 参数区分），main.c 内要做分支。
- **`apps/default/` 与 `apps/hello_world/` / `apps/bt_scan/` 共存**：build/burn 默认走 default；显式 `--app hello_world` / `--app bt_scan` 仍可用。三套独立二进制，互不影响。
- **`build.py` / `burn.py` 默认值改动**：是项目级约定变更；本次 commit 一并完成。

## 十二、本里程碑的"做完"

- `bluetooth/esp32-wroom-32e/apps/default/` 可 build +  + 抓 log
- board_a 烧 default 后，手柄切 BT 模式 → 3s 内自动连接 → HID 报告原始 hex 输出
- **手动按键**验证：不同按键 → 不同 hex 模式
- 断开重连：手柄关 → ESP32 close + 候选循环恢复；手柄重开 → 重连
- `tools/build.py` / `tools/burn.py` 默认值改为 `default`
- AGENTS.md / README.md / development.md / apps/default/README.md 全部更新
- 提交到 `bt-hid-host` 分支，未合并
- `hello_world` / `bt_scan` app 不动