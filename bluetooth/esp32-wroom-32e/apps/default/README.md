# default (项目主 app)

飞智八爪鱼5 自制接收器项目的**主 app**。从 BT HID 主机迭代起，所有蓝牙方向的功能迭代都更新这里，不新建其他 app。

`app_main` 完成后即返回；连接/重连/超时/退避全由 **GAP/HID 回调 + 三个 `esp_timer`** 推进，无 `while(1)` 轮询、无阻塞式 `esp_hid_scan`。

## 驱动模型

整条连接生命周期是事件链：

```
app_main
  nvs_flash_init → bt_stack_start → 注册 GAP 回调 → esp_hidh_init
  → boot bond 诊断 → 起步决策（有 bond → page bonded；无 bond → inquiry）
  → 返回

[ESP_BT_GAP_DISC_RES_EVT]        bt_gap_cb       解析 name/cod/rssi → EWMA → 候选层3
[ESP_BT_GAP_DISC_STATE_CHANGED]  bt_gap_cb       STOPPED → 续扫（保持连续 inquiry）
[esp_timer lock_tick 250ms]      lock_tick_cb    静默期重评锁定（stable≥3s || total≥8s）
[ESP_HIDH_OPEN_EVT]              hidh_event_handler  → note_connect_ok / disarm timeout
[ESP_HIDH_INPUT_EVT]             hidh_event_handler  → hid_decode（仅变化时打印）
[ESP_HIDH_CLOSE_EVT]             hidh_event_handler  → backoff_to_scan
[esp_timer connect_timeout 4s]   conn_timeout_cb     → note_connect_fail → 退避
[esp_timer rescan_backoff 300ms] rescan_backoff_cb   → 有 bond 先 page、否则 inquiry
```

并发保护：GAP 任务 vs timer 任务会并发改 `g_candidate` / `g_ewma` / `g_state`，用一把
`g_app_mutex` 串行化（临界区仅内存判定 + 非阻塞 BT API）。

## 模块划分

| 文件 | 职责 |
|---|---|
| `main/main.c` | 编排：GAP/HID 回调 + 三个 esp_timer 回调 + 候选/EWMA/迟滞 + 状态机护栏 |
| `main/bt_stack.{c,h}` | 底层 bring-up：Bluedroid(BTDM) + SSP 安全参数 + scan_mode；不注册 GAP 回调、不启动 discovery |
| `main/hid_report.{c,h}` | Report Descriptor dump + 15 字节输入报告 → `apex5_xinput_t` 解码，仅状态变化时打印（**未动**）|
| `main/CMakeLists.txt` | `SRCS` 含 `bt_stack.c` + `hid_report.c`（旧 `esp_hid_gap.c` 已删）|

### 关键约束（防误删）

`main.c` 在 `esp_hidh_init` 之前**必须保留**：
```c
esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler);
```
这是 `esp_hidh` 内部 BLE 分支的强制初始化管道（`esp_ble_hidh_init` 会 `gattc_app_register(0)` + `WAIT_CB()` 等 REG 事件），**不注册则 `esp_hidh_init` 永久阻塞，启动卡在 `[hid] boot bonds` 之前**。与本里程碑跑不跑 BLE 业务无关——是 esp_hidh 库自身的契约。详见 spec §四 / `fix(82bd08e..ed6fb7a)`。

## 三层候选算法（与重构前等价）

`name 白名单 → EWMA(α=0.3) → 迟滞 3dB → 锁定 stable≥3s || total≥8s`。判定逻辑与常量不变，触发方式由「每 3s 批处理」改为「每条 `DISC_RES_EVT` 即时 + 250ms tick 重评」。`gp_count` 仅诊断打印，不参与锁定。详见 spec §六。

候选筛选白名单 `g_gamepad_names`：`Xbox Wireless Controller`（X-input）+ `Pro Controller`（NS）。新增名字实测后再加。

## BLE 定位

- 底层 Bluedroid 仍 **BTDM 模式**（BR/EDR + BLE 双模使能），BLE 射频照常开。
- 本里程碑**只跑 BR/EDR 发现/连接流程**，**不实现 BLE 业务**。原因：`bt_scan` 实测 X-input 模式下 BLE 命中 0；HID descriptor 另有 `id=2/4 INPUT + id=3 OUTPUT` vendor 通道，疑走 BR/EDR vendor report 而非 BLE GATT，但未确证。
- 后续如需 BLE GATT（配置/OTA 走 GATT），按 spec §四「BLE 定位」增量补 `ble_gatt.{c,h}`，**不动**本 BR/EDR 回调链。

## 构建 + 烧录

```bash
source /opt/esp-idf/export.sh
python3 bluetooth/esp32-wroom-32e/tools/build.py --app default
python3 bluetooth/esp32-wroom-32e/tools/burn.py --board-a          # --board-a 必填
python3 tools/capture_uart.py --board-a --rst-a --duration 60 --odir /tmp --ts
```

`burn.py` 必须显式 `--board-a`（端口读 `.env`，DTR 复位由 `BOARD_A_TYPE=esp32-wroom-32e` 决定，无需 `RST_PORT`）。

测试前提醒：手柄拨杆到 BT（蓝色 LED 位）、长按配对键进入快闪可发现态；空闲 10–30s 进省电停止广播，需先按回配对态再跑命令。

## 验证记录

三场景 2026-09-06 真机验证通过（host 起始 `bonds=0`）：

| 场景 | 时延 |
|---|---|
| ① 全新首配 | candidate → open ≈ **2.4s** |
| ② bonded 重连 | page → open ≈ **1.5s** |
| ③ 手柄侧残留旧 bond | ~30s 内自动清键 + 打印 `re-pair if needed` + 回 `bonds=0` |

完整事件时间线与日志片段见 [`docs/superpowers/specs/2026-09-06-bt-hid-callback-refactor-design.md`](../../../../docs/superpowers/specs/2026-09-06-bt-hid-callback-refactor-design.md) §十二。

## 设计文档

- 设计 spec：[`docs/superpowers/specs/2026-09-06-bt-hid-callback-refactor-design.md`](../../../../docs/superpowers/specs/2026-09-06-bt-hid-callback-refactor-design.md)
- 实施计划：[`docs/superpowers/plans/2026-09-06-bt-hid-callback-refactor.md`](../../../../docs/superpowers/plans/2026-09-06-bt-hid-callback-refactor.md)
- 前序（轮询版 + 解码）：[`2026-09-05-bt-hid-host-capture-design.md`](../../../../docs/superpowers/specs/2026-09-05-bt-hid-host-capture-design.md)、[`2026-09-05-bt-hid-report-decode-design.md`](../../../../docs/superpowers/specs/2026-09-05-bt-hid-report-decode-design.md)
