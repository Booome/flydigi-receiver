# BT HID 主机回调链重构 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 去掉 `apps/default/` 的 `while(1)` 轮询与阻塞式 `esp_hid_scan`，改为纯回调链驱动（自管异步 GAP discovery + `esp_timer`），行为等价、实时性更好。

**Architecture:** BR/EDR-only 事件流。`app_main` 只做一次起步决策后返回；发现/连接/重连全部由 `esp_bt_gap` 回调（`DISC_RES_EVT`/`DISC_STATE_CHANGED_EVT`/SSP）+ `esp_hidh` 回调（`OPEN/INPUT/CLOSE`）+ 三个 `esp_timer`（`lock_tick`/`connect_timeout`/`rescan_backoff`）逐段推进。三层候选算法（EWMA/迟滞/锁定窗）判定逻辑与常量保持不变，仅由"每 3s 批处理"改为"逐 `DISC_RES_EVT` 更新 + `lock_tick` 周期评估"。共享态用一把 `g_app_mutex` 串行化。

**Tech Stack:** ESP-IDF v6.0.2（`/opt/esp-idf` 只读）、Bluedroid（BTDM）、`esp_hidh`、FreeRTOS、`esp_timer`。目标板 ESP32-WROOM-32E（board_a）。

## Global Constraints

- **仓库根**：`/home/bodong/workspace/flydigi-receiver`；**全部改动在 worktree** `.worktrees/bt-hid-callback-refactor/`（分支 `bt-hid-callback-refactor`），主工作区保持干净。
- **永不自动 commit**（AGENTS.md）：计划中的 commit 步骤仅在**用户明确授权**后执行；本仓库所有提交由用户发起。
- **不新增 BLE 业务流程，也不削减 BLE 能力**：控制器保持 **BTDM**（BR/EDR+BLE 双使能），sdkconfig 不改；`esp_hid_gap.c` 的删除 **≠** 关 BLE 射频（详见 spec §四「BLE 定位」）。
- **行为等价**：候选→open 触发仍为 `stable ≥ LOCK_WAIT_MS(3000)` 或 `total ≥ MAX_WAIT_MS(8000)`；SSP Just Works、bond 冷启动 page、power-cycle reconnect、OPEN-fail 清键、bare-timeout 保 bond、`note_connect_fail` 累计 3 次才清键——**全部保持**。
- **解码不动**：`hid_report.{c,h}` 内容不改（按钮位映射的实测校正是**另一独立任务**，不在本计划）。`HID_DEBUG_DELTA` 维持现状。
- **格式化铁律**（AGENTS.md）：改任一 `.c/.h` 立即 `clang-format -i`；改 `CMakeLists.txt` 立即 `cmake-format -c .cmake-format.yaml -i`；提交前 `clang-format --dry-run --Werror` 全绿。
- **C 文件分节顺序**：include→define→type→global→前向声明→函数定义；不得在函数后插全局/宏/前向声明。
- **禁止 `(void)arg`**：项目已开 `-Wno-unused-parameter`，未用参数裸写。
- **面向人读中文、面向机器/仓库英文**：代码/注释/标识符/commit message/日志字段前缀（`[hid]`、`[gap]`）英文。
- **手柄协同**：涉及真机验证的任务，先 `bash tools/notify.sh` 提示，等用户把手柄拨到 BT 模式并长按配对键（蓝灯快闪）回复"好了"再跑；手柄 10–30s 省电。
- 构建：`source /opt/esp-idf/export.sh` → `python3 bluetooth/esp32-wroom-32e/tools/build.py --app default`。烧录：`python3 bluetooth/esp32-wroom-32e/tools/burn.py --board-a`。抓 log：`python3 tools/capture_uart.py --board-a --rst-a --duration N --odir /tmp --ts`。`.env`（worktree 内若缺则从主库根复制）：`BOARD_A_PORT` + `BOARD_A_TYPE=esp32-wroom-32e`。

## File Structure

```
bluetooth/esp32-wroom-32e/apps/default/main/
├── bt_stack.c        # 新建：BR/EDR/Bluedroid BTDM bring-up + SSP 安全参数 + scan_mode；bt_stack_start()
├── bt_stack.h        # 新建：bt_stack_start() 声明
├── main.c            # 重写：回调链（gap_cb + hidh_cb + 3×esp_timer + 候选/EWMA + mutex）；无 while(1)/esp_hid_scan
├── esp_hid_gap.c     # 删除
├── esp_hid_gap.h     # 删除
└── CMakeLists.txt    # SRCS: esp_hid_gap.c → bt_stack.c；去 esp_hid_gap
docs/superpowers/specs/2026-09-06-bt-hid-callback-refactor-design.md  # 设计依据（已存在）
bluetooth/esp32-wroom-32e/apps/default/README.md                       # 文档同步（Task 5）
AGENTS.md                                                              # 里程碑状态同步（Task 5）
```

每任务交付物均可独立编译通过（Task 2 结束时 `esp_hid_gap.c` 暂留为死代码；Task 3 删除）。

---

## Task 1: `bt_stack.{c,h}` 底层 bring-up 模块

**Files:**
- Create: `bluetooth/esp32-wroom-32e/apps/default/main/bt_stack.c`
- Create: `bluetooth/esp32-wroom-32e/apps/default/main/bt_stack.h`
- Modify: `bluetooth/esp32-wroom-32e/apps/default/main/CMakeLists.txt`（`srcs` 加 `bt_stack.c`）

**Interfaces:**
- Consumes: 无（自 `esp_hid_gap.c` 的 `init_low_level` Bluedroid 分支 + `init_bt_gap` 抽取）。
- Produces: `esp_err_t bt_stack_start(void);` —— 完成 controller+Bluedroid(BTDM) 使能、SSP
  `iocap=NONE`、legacy pin、`set_scan_mode(CONNECTABLE, NON_DISCOVERABLE)`。**不**注册 gap 回调、
  **不**起扫描。Task 2 的 `app_main` 调它。

> 说明：本任务只**新增**文件 + 加进 SRCS，`main.c` 仍用旧 `esp_hid_gap`，二者共存、`bt_stack_start`
> 暂无人调用也须能编译（`-Wunused-function` 对非 static 的 `bt_stack_start` 不触发）。

- [ ] **Step 1: 写 `bt_stack.h`**

```c
/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#ifndef BT_APP_BT_STACK_H
#define BT_APP_BT_STACK_H

#include "esp_err.h"

/* Bring up BR/EDR (Bluedroid, BTDM mode) + SSP/security params + scan mode.
 * Does NOT register the GAP callback nor start discovery (caller does). */
esp_err_t bt_stack_start(void);

#endif /* BT_APP_BT_STACK_H */
```

- [ ] **Step 2: 写 `bt_stack.c`**（`#if CONFIG_BT_CLASSIC_ENABLED` 内即 Bluedroid 非 NimBLE 路径）

```c
/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * Minimal BR/EDR (Bluedroid BTDM) bring-up extracted from esp_hid_gap:
 * controller + Bluedroid enable, SSP NoInputNoOutput (headless Just Works),
 * legacy pin, connectable+non-discoverable scan mode. GAP callback and
 * discovery are the caller's (main.c) responsibility.
 */

#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "bt_stack.h"

static const char *TAG = "bt_stack";

esp_err_t bt_stack_start(void)
{
    esp_err_t ret;

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.mode = ESP_BT_MODE_BTDM;
    bt_cfg.bt_max_acl_conn = 3;
    bt_cfg.bt_max_sync_conn = 3;

    if ((ret = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "controller init failed: %d", ret);
        return ret;
    }
    if ((ret = esp_bt_controller_enable(ESP_BT_MODE_BTDM)) != ESP_OK) {
        ESP_LOGE(TAG, "controller enable failed: %d", ret);
        return ret;
    }

    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid init failed: %d", ret);
        return ret;
    }
    if ((ret = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid enable failed: %d", ret);
        return ret;
    }

    /* Headless host: advertise NoInputNoOutput so SSP uses Just Works and a
     * reconnect never waits on a passkey/display the user cannot provide. */
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;
    esp_bt_gap_set_pin(pin_type, 0, pin_code);

    /* Allow bonded peers to page us back; we are not ourselves discoverable. */
    if ((ret = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE)) != ESP_OK) {
        ESP_LOGE(TAG, "set_scan_mode failed: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "BR/EDR (BTDM) ready");
    return ESP_OK;
}
```

- [ ] **Step 3: 改 `CMakeLists.txt` 的 `srcs`**（先只加 `bt_stack.c`，暂不动 `esp_hid_gap.c`）

把第 1 行改为：

```cmake
set(srcs "main.c" "esp_hid_gap.c" "hid_report.c" "bt_stack.c")
```

- [ ] **Step 4: 编译验证**

Run: `source /opt/esp-idf/export.sh && python3 bluetooth/esp32-wroom-32e/tools/build.py --app default`
Expected: `Project build complete`，无新增 `error:`/`warning:`。

- [ ] **Step 5: 格式化**

Run: `clang-format -i bluetooth/esp32-wroom-32e/apps/default/main/bt_stack.c bluetooth/esp32-wroom-32e/apps/default/main/bt_stack.h && cmake-format -c .cmake-format.yaml -i bluetooth/esp32-wroom-32e/apps/default/main/CMakeLists.txt`（在**仓库根**执行，`.cmake-format.yaml` 在根）
再 `clang-format --dry-run --Werror` 两文件 → 无输出即通过。

- [ ] **Step 6: 提交（仅在用户授权时）**

```bash
git add bluetooth/esp32-wroom-32e/apps/default/main/bt_stack.c bluetooth/esp32-wroom-32e/apps/default/main/bt_stack.h bluetooth/esp32-wroom-32e/apps/default/main/CMakeLists.txt
git commit -m "feat(esp32): add bt_stack bring-up module (BR/EDR BTDM + SSP Just Works)"
```

---

## Task 2: `main.c` 重写为回调链

**Files:**
- Modify: `bluetooth/esp32-wroom-32e/apps/default/main/main.c`（整文件替换）

**Interfaces:**
- Consumes: `bt_stack_start()`（Task 1）、`hid_dump_report_map/hid_decode/hid_print_state/apex5_xinput_t/HID_DEBUG_DELTA/BTN_*`（`hid_report.h`，不变）、`esp_bt_gap_*`、`esp_hidh_*`、`esp_timer_*`。
- Produces: 完整事件驱动 app；`app_main` 起步后返回，无循环。（本任务结束时 `esp_hid_gap.c` 已不再被 `main.c` 引用，成为死代码，Task 3 删除。）

> **本仓库无单测框架**（嵌入式）。Task 的"测试"= ①`build.py` 编译门 ②`clang-format --dry-run --Werror` 格式门 ③`grep` 静态不变式（无 `while (1)`、无 `esp_hid_scan`）；真机行为验证在 Task 4。

- [ ] **Step 1: 用下面整份内容替换 `main.c`**

```c
/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * default: BT HID host, event/callback-driven (no polling loop).
 *
 * app_main brings the stack up, registers the GAP + HID callbacks + timers,
 * makes ONE start decision (page a bonded peer, else continuous inquiry), then
 * returns. Thereafter every transition is driven by GAP DISC_RES/DISC_STATE,
 * HID OPEN/INPUT/CLOSE, and three esp_timer ticks. No while(1), no esp_hid_scan.
 *
 * Selection (unchanged 3-layer): gamepad name allowlist -> EWMA(0.3) ->
 * hysteresis 3dB -> lock when stable>=3s or total>=8s. Re-evaluated on a 250ms
 * tick so a quiet link still locks on schedule. See
 * docs/superpowers/specs/2026-09-06-bt-hid-callback-refactor-design.md
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_bt_api.h"
#include "esp_hidh.h"
#include "bt_stack.h"
#include "hid_report.h"

#define TAG "default"

/* Windows/constants unchanged from the polling implementation. */
#define HYSTERESIS_DB 3
#define LOCK_WAIT_MS 3000
#define MAX_WAIT_MS 8000
#define CONNECT_TIMEOUT_MS 4000 /* open() with no OPEN/CLOSE event -> give up */
#define RETRY_BACKOFF_MS 300    /* pause after a fail/close before resuming */
#define CANDIDATE_GONE_MS 6000  /* candidate not sighted this long -> drop it */
#define LOCK_TICK_MS 250        /* periodic re-evaluation cadence */
#define INQ_LENGTH 8            /* esp_bt_gap_start_discovery length (8*1.28s) */
#define EWMA_ALPHA_NUM 3        /* /10 -> alpha=0.3 */
#define EWMA_ALPHA_DEN 10
#define EWMA_MAX 16
#define CONNECT_FAIL_HINT_AFTER 3

typedef struct {
    uint8_t bda[6];
    bool used;
    float smoothed;
    int64_t last_seen_ms;
} ewma_entry_t;

typedef struct {
    bool active;
    uint8_t bda[6];
    float smoothed;
    int64_t set_at_ms;
} candidate_t;

/* Guard only (which side-effects are allowed); transitions are event-driven. */
typedef enum { ST_SCANNING, ST_CONNECTING, ST_CONNECTED } app_state_t;

static ewma_entry_t g_ewma[EWMA_MAX];
static candidate_t g_candidate = {0};
static int64_t g_scan_start_ms = 0;
static volatile app_state_t g_state = ST_SCANNING;
static esp_bd_addr_t g_fail_bda = {0};
static int g_fail_count = 0;
static SemaphoreHandle_t g_app_mutex = NULL;

static esp_timer_handle_t g_lock_tick = NULL;      /* periodic, SCANNING only */
static esp_timer_handle_t g_conn_timeout = NULL;   /* one-shot, armed on open */
static esp_timer_handle_t g_rescan_backoff = NULL; /* one-shot */

/* Known Apex5 BT identities (BR/EDR advertised name). Extend as new names appear. */
static const char *const g_gamepad_names[] = {
    "Xbox Wireless Controller", /* PC>Bluetooth / Android / iOS = X-input */
    "Pro Controller",           /* Nintendo Switch (NS) mode             */
};

static void lock(void)
{
    xSemaphoreTake(g_app_mutex, portMAX_DELAY);
}
static void unlock(void)
{
    xSemaphoreGive(g_app_mutex);
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void print_bda(const uint8_t *bda)
{
    printf("%02x:%02x:%02x:%02x:%02x:%02x", bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

static uint8_t bda_eq(const uint8_t *a, const uint8_t *b)
{
    return memcmp(a, b, 6) == 0;
}

static bool is_gamepad(const char *name)
{
    if (!name) {
        return false;
    }
    for (size_t i = 0; i < sizeof(g_gamepad_names) / sizeof(g_gamepad_names[0]); i++) {
        if (strcasecmp(name, g_gamepad_names[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void ewma_clear(void)
{
    for (int i = 0; i < EWMA_MAX; i++) {
        g_ewma[i].used = false;
    }
}

static ewma_entry_t *ewma_find(const uint8_t *bda)
{
    for (int i = 0; i < EWMA_MAX; i++) {
        if (g_ewma[i].used && bda_eq(g_ewma[i].bda, bda)) {
            return &g_ewma[i];
        }
    }
    return NULL;
}

static ewma_entry_t *ewma_get_or_create(const uint8_t *bda)
{
    ewma_entry_t *e = ewma_find(bda);
    if (e) {
        return e;
    }
    for (int i = 0; i < EWMA_MAX; i++) {
        if (!g_ewma[i].used) {
            memcpy(g_ewma[i].bda, bda, 6);
            g_ewma[i].used = true;
            g_ewma[i].smoothed = -127.0f;
            return &g_ewma[i];
        }
    }
    return NULL;
}

/* 层2 EWMA + record sighting time. Returns the smoothed value. */
static float ewma_update(const uint8_t *bda, int8_t rssi, int64_t t)
{
    ewma_entry_t *e = ewma_get_or_create(bda);
    if (!e) {
        return -127.0f;
    }
    if (e->smoothed == -127.0f) {
        e->smoothed = (float)rssi;
    } else {
        e->smoothed = (float)rssi * EWMA_ALPHA_NUM / EWMA_ALPHA_DEN +
                      e->smoothed * (EWMA_ALPHA_DEN - EWMA_ALPHA_NUM) / EWMA_ALPHA_DEN;
    }
    e->last_seen_ms = t;
    return e->smoothed;
}

/* 层3 迟滞 + 候选计时 (no lock decision here; lock_tick decides). */
static void candidate_update(const uint8_t *bda, float smoothed, int64_t t)
{
    if (!g_candidate.active) {
        g_candidate.active = true;
        memcpy(g_candidate.bda, bda, 6);
        g_candidate.smoothed = smoothed;
        g_candidate.set_at_ms = t;
        printf("[hid] candidate: addr=");
        print_bda(bda);
        printf(" smoothed=%.1f\n", smoothed);
    } else if (bda_eq(bda, g_candidate.bda)) {
        /* same candidate; lock clock keeps running */
    } else if (smoothed >= g_candidate.smoothed + HYSTERESIS_DB) {
        g_candidate.active = true;
        memcpy(g_candidate.bda, bda, 6);
        g_candidate.smoothed = smoothed;
        g_candidate.set_at_ms = t;
        printf("[hid] candidate replace: addr=");
        print_bda(bda);
        printf(" smoothed=%.1f\n", smoothed);
    }
    /* else: near-tie, keep existing candidate + clock */
}

static void remove_bond_of(const uint8_t *bda)
{
    if (!bda) {
        return;
    }
    esp_bd_addr_t peer;
    memcpy(peer, bda, sizeof(esp_bd_addr_t));
    esp_bt_gap_remove_bond_device(peer);
    printf("[hid] bond removed for ");
    print_bda(bda);
    printf(" (bond_num=%d)\n", esp_bt_gap_get_bond_device_num());
}

/* Count consecutive silent failures to one addr; clear a hopeless bond after N.
 * Returns the backoff delay (ms) to use for this failure. Caller holds lock. */
static int64_t note_connect_fail(const uint8_t *bda)
{
    if (bda && memcmp(bda, g_fail_bda, sizeof(esp_bd_addr_t)) == 0) {
        g_fail_count++;
    } else {
        memcpy(g_fail_bda, bda ? bda : g_fail_bda, sizeof(esp_bd_addr_t));
        g_fail_count = 1;
    }
    if (g_fail_count == CONNECT_FAIL_HINT_AFTER) {
        printf("[hid] %d connect fails to ", g_fail_count);
        print_bda(g_fail_bda);
        printf(" - dropping bond, re-pair if needed\n");
        remove_bond_of(g_fail_bda);
        return RETRY_BACKOFF_MS * 4;
    }
    return RETRY_BACKOFF_MS;
}

static void note_connect_ok(const uint8_t *bda)
{
    (void)bda;
    g_fail_count = 0;
    memset(g_fail_bda, 0, sizeof(g_fail_bda));
}

/* ---- side-effect helpers (all called with lock held) ---- */

static void arm_rescan(int64_t delay_ms)
{
    esp_timer_start_once(g_rescan_backoff, (uint64_t)delay_ms * 1000);
}

static void begin_scan_round(void)
{
    memset(&g_candidate, 0, sizeof(g_candidate));
    ewma_clear();
    g_scan_start_ms = now_ms();
    g_state = ST_SCANNING;
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, INQ_LENGTH, 0);
    esp_timer_start_periodic(g_lock_tick, (uint64_t)LOCK_TICK_MS * 1000);
}

/* stop scanning side-effects before we take the radio for an open. */
static void halt_scanning_side_effects(void)
{
    esp_bt_gap_cancel_discovery();
    esp_timer_stop(g_lock_tick);
}

static void open_candidate(void)
{
    halt_scanning_side_effects();
    esp_timer_start_once(g_conn_timeout, (uint64_t)CONNECT_TIMEOUT_MS * 1000);
    g_state = ST_CONNECTING;
    printf("[hid] connecting: addr=");
    print_bda(g_candidate.bda);
    printf(" transport=BR_EDR\n");
    esp_hidh_dev_open(g_candidate.bda, ESP_HID_TRANSPORT_BT, 0);
}

static void open_bonded(void)
{
    int nb = esp_bt_gap_get_bond_device_num();
    if (nb <= 0) {
        return;
    }
    esp_bd_addr_t list[8];
    int want = nb > 8 ? 8 : nb;
    if (esp_bt_gap_get_bond_device_list(&want, list) != ESP_OK || want <= 0) {
        return;
    }
    memcpy(g_candidate.bda, list[0], sizeof(esp_bd_addr_t));
    g_candidate.smoothed = 0;
    g_candidate.active = true;
    g_candidate.set_at_ms = now_ms();
    halt_scanning_side_effects();
    esp_timer_start_once(g_conn_timeout, (uint64_t)CONNECT_TIMEOUT_MS * 1000);
    g_state = ST_CONNECTING;
    printf("[hid] outbound page bonded ");
    print_bda(g_candidate.bda);
    printf(" (bonds=%d)\n", want);
    esp_hidh_dev_open(g_candidate.bda, ESP_HID_TRANSPORT_BT, 0);
}

/* Resume after boot / fail / close: page the bonded peer if any, else scan. */
static void resume_scan(void)
{
    if (esp_bt_gap_get_bond_device_num() > 0) {
        open_bonded();
    } else {
        begin_scan_round();
    }
}

/* ---- esp_timer callbacks (run in the esp_timer task) ---- */

static void lock_tick_cb(void *arg)
{
    lock();
    if (g_state != ST_SCANNING) {
        unlock();
        return;
    }
    int64_t t = now_ms();

    if (g_candidate.active) {
        ewma_entry_t *e = ewma_find(g_candidate.bda);
        if (e && (t - e->last_seen_ms) > CANDIDATE_GONE_MS) {
            printf("[hid] candidate gone\n");
            memset(&g_candidate, 0, sizeof(g_candidate));
        }
    }

    if (g_candidate.active) {
        int64_t stable = t - g_candidate.set_at_ms;
        int64_t total = t - g_scan_start_ms;
        if (stable >= LOCK_WAIT_MS || total >= MAX_WAIT_MS) {
            open_candidate();
        }
    }
    unlock();
}

static void conn_timeout_cb(void *arg)
{
    lock();
    if (g_state != ST_CONNECTING) {
        unlock();
        return; /* OPEN/CLOSE already resolved this attempt */
    }
    printf("[hid] connect timeout, rescan\n");
    esp_bd_addr_t tried;
    memcpy(tried, g_candidate.bda, sizeof(tried));
    memset(&g_candidate, 0, sizeof(g_candidate));
    ewma_clear();
    g_state = ST_SCANNING;
    int64_t delay = note_connect_fail(tried);
    arm_rescan(delay);
    unlock();
}

static void rescan_backoff_cb(void *arg)
{
    lock();
    if (g_state == ST_CONNECTED) {
        unlock();
        return; /* raced with a successful open; stay connected */
    }
    resume_scan();
    unlock();
}

/* ---- GAP callback (runs in the BT task) ---- */

static void handle_disc_result(esp_bt_gap_cb_param_t *p)
{
    const uint8_t *bda = p->disc_res.bda;
    char nm[64] = {0};
    const char *name = NULL;
    int8_t rssi = 0;

    for (int i = 0; i < p->disc_res.num_prop; i++) {
        esp_bt_gap_dev_prop_t *prop = &p->disc_res.prop[i];
        if (prop->type == ESP_BT_GAP_DEV_PROP_BDNAME) {
            name = (const char *)prop->val;
        } else if (prop->type == ESP_BT_GAP_DEV_PROP_RSSI) {
            rssi = *((int8_t *)prop->val);
        } else if (prop->type == ESP_BT_GAP_DEV_PROP_EIR) {
            uint8_t len = 0;
            uint8_t *d = esp_bt_gap_resolve_eir_data(
                (uint8_t *)prop->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &len
            );
            if (!d) {
                d = esp_bt_gap_resolve_eir_data(
                    (uint8_t *)prop->val, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &len
                );
            }
            if (d && len && !name) {
                if (len > sizeof(nm) - 1) {
                    len = sizeof(nm) - 1;
                }
                memcpy(nm, d, len);
                nm[len] = 0;
                name = nm;
            }
        }
    }

    if (!is_gamepad(name)) {
        return;
    }
    lock();
    if (g_state == ST_SCANNING) {
        int64_t t = now_ms();
        float s = ewma_update(bda, rssi, t);
        if (s != -127.0f) {
            candidate_update(bda, s, t);
        }
    }
    unlock();
}

static void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT:
        handle_disc_result(param);
        break;
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        /* Continuous inquiry: Bluedroid stops inquiry after INQ_LENGTH;
         * restart immediately while still scanning (keeps candidate/EWMA). */
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            lock();
            if (g_state == ST_SCANNING) {
                esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, INQ_LENGTH, 0);
            }
            unlock();
        }
        break;
    case ESP_BT_GAP_CFM_REQ_EVT:
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_REQ_EVT:
        /* Headless host: no keyboard. Accept with passkey 0 so SSP never stalls. */
        esp_bt_gap_ssp_passkey_reply(param->key_req.bda, true, 0);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        printf("[gap] KEY_NOTIF passkey=%06" PRIu32 "\n", param->key_notif.passkey);
        break;
    case ESP_BT_GAP_PIN_REQ_EVT: {
        esp_bt_pin_code_t pin = {0};
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin);
        break;
    }
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        printf(
            "[gap] AUTH_CMPL addr=%02x:%02x:%02x:%02x:%02x:%02x name=%s stat=%d %s lk=%d\n",
            param->auth_cmpl.bda[0],
            param->auth_cmpl.bda[1],
            param->auth_cmpl.bda[2],
            param->auth_cmpl.bda[3],
            param->auth_cmpl.bda[4],
            param->auth_cmpl.bda[5],
            (const char *)param->auth_cmpl.device_name,
            (int)param->auth_cmpl.stat,
            (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) ? "OK" : "FAIL",
            (int)param->auth_cmpl.lk_type
        );
        break;
    default:
        break;
    }
}

/* ---- HID callback (runs in the esp_hidh event task) ---- */

static void
hidh_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)event_data;
    const uint8_t *bda = NULL;

    switch (event_id) {
    case ESP_HIDH_OPEN_EVENT:
        bda = esp_hidh_dev_bda_get(param->open.dev);
        esp_timer_stop(g_conn_timeout);
        if (param->open.status == ESP_OK) {
            lock();
            halt_scanning_side_effects();
            g_state = ST_CONNECTED;
            memset(&g_candidate, 0, sizeof(g_candidate));
            note_connect_ok(bda);
            unlock();
            printf("[hid] open: addr=");
            print_bda(bda);
            printf(" transport=BR_EDR\n");
            hid_dump_report_map(param->open.dev);
        } else {
            printf("[hid] open FAIL: addr=");
            print_bda(bda);
            printf(" transport=BR_EDR status=0x%x\n", (unsigned)param->open.status);
            if (param->open.dev) {
                esp_hidh_dev_free(param->open.dev);
            }
            lock();
            remove_bond_of(bda); /* peer rejected our key -> drop it, next round pairs fresh */
            int64_t delay = note_connect_fail(bda);
            memset(&g_candidate, 0, sizeof(g_candidate));
            ewma_clear();
            g_state = ST_SCANNING;
            arm_rescan(delay);
            unlock();
        }
        break;
    case ESP_HIDH_INPUT_EVENT: {
        apex5_xinput_t cur;
        if (!hid_decode(param->input.data, param->input.length, &cur)) {
            break;
        }
#if HID_DEBUG_DELTA
        static uint8_t raw_prev[64];
        static uint16_t raw_prev_len = 0;
        if (param->input.length <= sizeof(raw_prev)) {
            for (uint16_t i = 0; i < param->input.length; i++) {
                if (i >= raw_prev_len || raw_prev[i] != param->input.data[i]) {
                    printf(
                        "[hid] d b%u:%02x>%02x ", i, i < raw_prev_len ? raw_prev[i] : 0,
                        param->input.data[i]
                    );
                }
            }
            printf("\n");
            memcpy(raw_prev, param->input.data, param->input.length);
            raw_prev_len = param->input.length;
        }
#endif
        static apex5_xinput_t prev;
        static bool have_prev = false;
        if (!have_prev || memcmp(&prev, &cur, sizeof(cur)) != 0) {
            hid_print_state(&cur);
            prev = cur;
            have_prev = true;
        }
        break;
    }
    case ESP_HIDH_CLOSE_EVENT:
        bda = esp_hidh_dev_bda_get(param->close.dev);
        printf("[hid] close: addr=");
        print_bda(bda);
        printf(" transport=BR_EDR status=0x%x\n", (unsigned)param->close.status);
        if (param->close.dev) {
            esp_hidh_dev_free(param->close.dev);
        }
        lock();
        g_state = ST_SCANNING; /* bond kept; resume_scan() will page it */
        arm_rescan(RETRY_BACKOFF_MS);
        unlock();
        break;
    default:
        break;
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(bt_stack_start());

    ESP_ERROR_CHECK(esp_bt_gap_register_callback(bt_gap_cb));

    esp_hidh_config_t hidh_cfg = {
        .callback = hidh_event_handler,
        .event_stack_size = 4096,
        .callback_arg = NULL,
    };
    ESP_ERROR_CHECK(esp_hidh_init(&hidh_cfg));

    g_app_mutex = xSemaphoreCreateMutex();

    const esp_timer_create_args_t tick_args = {
        .callback = lock_tick_cb,
        .name = "lock_tick",
    };
    const esp_timer_create_args_t ct_args = {.callback = conn_timeout_cb, .name = "conn_timeout"};
    const esp_timer_create_args_t rb_args = {
        .callback = rescan_backoff_cb,
        .name = "rescan_backoff",
    };
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &g_lock_tick));
    ESP_ERROR_CHECK(esp_timer_create(&ct_args, &g_conn_timeout));
    ESP_ERROR_CHECK(esp_timer_create(&rb_args, &g_rescan_backoff));

    printf("[hid] boot bonds=%d\n", esp_bt_gap_get_bond_device_num());

    lock();
    resume_scan();
    unlock();
    /* No loop: GAP/HID callbacks + esp_timer drive everything from here. */
}
```

- [ ] **Step 2: 编译门**

Run: `source /opt/esp-idf/export.sh && python3 bluetooth/esp32-wroom-32e/tools/build.py --app default`
Expected: `Project build complete`，无 `error:`。允许出现"esp_hid_gap.c 里 `esp_hid_scan` 未被引用"（外部死代码，不算 warning）。

- [ ] **Step 3: 静态不变式**

Run: `grep -nE 'while *\(1\)|esp_hid_scan|esp_hid_gap' bluetooth/esp32-wroom-32e/apps/default/main/main.c`
Expected: **无输出**（`main.c` 不再含轮询/阻塞扫描/对 esp_hid_gap 的 include）。

- [ ] **Step 4: 格式门**

Run: `clang-format -i bluetooth/esp32-wroom-32e/apps/default/main/main.c && clang-format --dry-run --Werror bluetooth/esp32-wroom-32e/apps/default/main/main.c`
Expected: 无输出（已格式化）。

- [ ] **Step 5: 提交（仅在用户授权时）**

```bash
git add bluetooth/esp32-wroom-32e/apps/default/main/main.c
git commit -m "refactor(esp32): drive connect lifecycle via GAP/HID callbacks + esp_timer (drop polling loop)"
```

---

## Task 3: 删除 `esp_hid_gap.{c,h}` 死代码

**Files:**
- Delete: `bluetooth/esp32-wroom-32e/apps/default/main/esp_hid_gap.c`
- Delete: `bluetooth/esp32-wroom-32e/apps/default/main/esp_hid_gap.h`
- Modify: `bluetooth/esp32-wroom-32e/apps/default/main/CMakeLists.txt`（`srcs` 去掉 `esp_hid_gap.c`）

**Interfaces:**
- Consumes: Task 2 的 `main.c`（已不引用 esp_hid_gap 的任何符号）。
- Produces: 干净构建，无 esp_hid_gap 依赖。

- [ ] **Step 1: 从 CMakeLists 去掉 esp_hid_gap.c**

`set(srcs ...)` 改为：

```cmake
set(srcs "main.c" "bt_stack.c" "hid_report.c")
```

- [ ] **Step 2: 删两文件**

```bash
git rm bluetooth/esp32-wroom-32e/apps/default/main/esp_hid_gap.c bluetooth/esp32-wroom-32e/apps/default/main/esp_hid_gap.h
```

- [ ] **Step 3: 确认无悬空引用**

Run: `grep -rn "esp_hid_gap\|esp_hid_scan" bluetooth/esp32-wroom-32e/apps/default/`
Expected: **无输出**（README 里的历史提及不算；只查源码目录）。

- [ ] **Step 4: 编译门**

Run: `source /opt/esp-idf/export.sh && python3 bluetooth/esp32-wroom-32e/tools/build.py --app default`
Expected: `Project build complete`，无 `error:`（此时 esp_hid_gap 的 BLE 依赖也没了，若报缺头说明 Task 2 有残留引用，回查）。

- [ ] **Step 5: 提交（仅在用户授权时）**

```bash
git add bluetooth/esp32-wroom-32e/apps/default/main/CMakeLists.txt
git commit -m "refactor(esp32): remove esp_hid_gap (superseded by bt_stack + callback chain)"
```

---

## Task 4: 真机行为验证（回调链不回退）

**Files:** 无代码改动；产出验证记录，必要时回填常量/修复。

**前置：** worktree 根有 `.env`（无则 `cp ../../.env .` 之类从主库根复制）：`BOARD_A_PORT=/dev/serial/by-path/...3.4.1.2...` + `BOARD_A_TYPE=esp32-wroom-32e`。板子已插 board_a 槽。

- [ ] **Step 1: 编译 + 烧录**

```bash
source /opt/esp-idf/export.sh
python3 bluetooth/esp32-wroom-32e/tools/build.py --app default
python3 bluetooth/esp32-wroom-32e/tools/burn.py --board-a
```
Expected: `Done`（DTR 复位烧录成功）。

- [ ] **Step 2: 通知用户准备手柄（首配场景）**

```bash
bash tools/notify.sh
```
提示文案：*"请把手柄拨到蓝牙(X-input)模式、长按配对键到蓝灯快闪，回'好了'。"* 等用户确认。

- [ ] **Step 3: 抓首配（无 bond）场景**

```bash
rm -f /tmp/board_a_*.log
python3 tools/capture_uart.py --board-a --rst-a --duration 40 --odir /tmp --ts --no-echo
L=$(ls -t /tmp/board_a_*.log | head -1)
grep -aE "boot bonds|candidate:|connecting:|AUTH_CMPL|\[hid\] open:|open FAIL|state:" "$L" | grep -avE "^\s*$"
```
Expected：`boot bonds=0`（若之前已配过则走 Step 5）→ 连续 inquiry → `[hid] candidate: ... smoothed=` → `[hid] connecting:` → `[gap] AUTH_CMPL ... stat=0 OK lk=4` → `[hid] open:` → 周期 `[hid] state:`。
不变式：日志时间轴上**没有 200ms 固定节拍的轮询心跳**（tick 只在决策时打印）。

- [ ] **Step 4: 记录首配时延**

从 `[hid] candidate` 到 `[hid] open` 的墙钟（看日志 `[+s.mmm]` 前缀）应 ≤ 现状约 3–8s（连续 inquiry 逐事件更新，正常更快）。记下数值。

- [ ] **Step 5: 冷启动 bonded page（关手柄→重开，或已配对后重烧）**

手柄保持已配对；重启固件后：
```bash
python3 tools/capture_uart.py --board-a --rst-a --duration 12 --odir /tmp --ts --no-echo
L=$(ls -t /tmp/board_a_*.log | head -1)
grep -aE "boot bonds|outbound page bonded|AUTH_CMPL|\[hid\] open:" "$L"
```
Expected：`boot bonds=1` → `outbound page bonded b5:...` → `[hid] open:`（≤ ~2s）。

- [ ] **Step 6: power-cycle reconnect（手柄关机→开机进"连接中"）**

notify 用户：*"把手柄关机，等 3s 再开机（它会进回连'连接中'），回'好了'。"* 抓 40s：
```bash
python3 tools/capture_uart.py --board-a --rst-a --duration 40 --odir /tmp --ts --no-echo
L=$(ls -t /tmp/board_a_*.log | head -1)
grep -aE "\[hid\] close:|outbound page bonded|connect timeout|bond removed|\[hid\] open:|state:" "$L"
```
Expected：`close:` → backoff → `outbound page bonded` → `open:`（快，~1s 内）→ 恢复 `state:`。

- [ ] **Step 7: stale-key 自动恢复（手柄恢复出厂/重配对，我们留旧 bond）**

notify：*"把八爪鱼5 恢复出厂/重新进入配对，让我们这边旧 bond 失效，回'好了'。"* 抓 90s：
```bash
python3 tools/capture_uart.py --board-a --rst-a --duration 90 --odir /tmp --ts --no-echo
L=$(ls -t /tmp/board_a_*.log | head -1)
grep -aE "outbound page bonded|open FAIL|bond removed|candidate:|\[hid\] open:|connect fails" "$L"
```
Expected：`outbound page bonded` →（若旧键失效）`open FAIL ... 0xffffffff` → `bond removed` → 下一轮 `candidate:` → `AUTH_CMPL OK` → `open:`。**或**纯超时路径：多次 `connect timeout` 累计到 `connect fails ... dropping bond` 后回 inquiry 成功。两条路都应在 ≤ ~20s 收敛（不出现"分钟级/永不"）。

- [ ] **Step 8: 解码回归**

确认 `[hid] state:` 打印格式与重构前**完全一致**（解码未动）：同一空闲帧字节 → 同一行输出。若 `[hid] d b..>` 差分行也出现属 `HID_DEBUG_DELTA` 现状，保留。

- [ ] **Step 9: 记结果**

把各场景实测时延与日志片段贴回本计划末尾「验证记录」小节（Task 5 一并落盘）。若某场景失败 → 这是真 bug，用 systematic-debugging 定位后回 Task 2 改（不换设计）。

---

## Task 5: 文档同步 + 收尾

**Files:**
- Modify: `bluetooth/esp32-wroom-32e/apps/default/README.md`（架构说明：回调链、无轮询、bt_stack）
- Modify: `AGENTS.md`（蓝牙方向状态加一行：`apps/default` 已从轮询改回调链）
- Modify: `docs/superpowers/specs/2026-09-06-bt-hid-callback-refactor-design.md`（追加「验证记录」）
- Modify: 本计划文件（勾选完成项 + 验证记录）

- [ ] **Step 1: README 加"驱动模型"节**

在 `apps/default/README.md` 说明：连接生命周期由 GAP/HID 回调 + 三个 esp_timer 驱动，无 `while(1)`；
`bt_stack.c` 仅 BTDM bring-up；BLE 射频保留但本里程碑不接 BLE 业务（改配置/OTA 若走 GATT 再补
`ble_gatt` 模块）。引用 spec 路径。

- [ ] **Step 2: AGENTS.md 状态更新**

蓝牙方向补一句：`apps/default/` 连接/重连由轮询 `while(1)` 重构为回调链（异步 GAP discovery +
esp_timer），行为等价、实时性更好；BLE 能力保留未接业务。

- [ ] **Step 3: 验证记录回填 spec + 计划**

把 Task 4 各场景时延/日志贴进 spec 末尾「## 验证记录」。

- [ ] **Step 4: 全量格式闸门**

```bash
find . \( -name '*.c' -o -name '*.h' \) -not -path './.git/*' -not -path '*/build/*' -not -path './docs/reference/*' \
  -print0 | xargs -0 clang-format --dry-run --Werror
```
Expected: 退出码 0（无未格式化文件）。若有，`clang-format -i` 相应文件。

- [ ] **Step 5: 提交（仅在用户授权时）**

```bash
git add -A
git commit -m "docs(esp32): document callback-chain refactor + verification results"
```

---

## Self-Review 记录（写计划后）

- **Spec 覆盖**：删 while(1)/esp_hid_scan→Task2；自管异步 GAP discovery→Task2 `begin_scan_round`/`bt_gap_cb`；三层算法保留→Task2 `ewma_update`/`candidate_update`/`lock_tick_cb`（常量不变，锁定条件 `stable≥3s||total≥8s` 一致）；esp_timer 三件套→Task2；bond page/清键/超时保键→Task2 `open_bonded`/`conn_timeout_cb`/`note_connect_fail`；bt_stack 拆分 + 删 esp_hid_gap→Task1/Task3；BLE 保留 BTDM→Global Constraints + Task3 编译门验证无残留 BLE 依赖；解码不动→`INPUT_EVENT` 分支原样。均有点对点任务。
- **占位符**：无 TBD/TODO；所有代码步骤含完整代码。
- **类型一致**：`bt_stack_start(void)->esp_err_t`（Task1 定义、Task2 调用一致）；`hid_decode/hid_print_state/apex5_xinput_t/HID_DEBUG_DELTA` 用 `hid_report.h` 现签名；`note_connect_fail` 返回 `int64`（Task2 内一致）；timer 名/handle 三处一致。
- **风险点已落计划**：esp_timer 回调里 `lock()` 短暂阻塞（临界区仅内存判定 + 非阻塞 BT API，无死锁）；连续 inquiry 在 `DISC_STATE STOPPED` 续扫不重置候选；`resume_scan` 有 bond 先 page（覆盖 bonded pad 的 page-only 态）。

## 执行期修正（实现/实测暴露，计划正文代码需以此为准）

1. **`open_bonded()` 返回 `bool` + `resume_scan()` 回落扫描**（fix `140ef48..f6ef910`）：brief 原
   代码在 `bond_num>0` 但 `get_bond_device_list` 失败/空时会"什么都不启动"，`g_state` 卡在
   `ST_SCANNING` 且无 inquiry/timer → 永久失活（违反行为等价：旧 `try_open_bonded()` 会 fall-through
   去 inquiry）。已改为 no-op 时回落 `begin_scan_round()`。
2. **保留 `esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler)`**（fix `82bd08e..ed6fb7a`）：
   brief 里随阻塞扫描一并删了它，但 `esp_hidh_init` 的 BLE 分支 `esp_ble_hidh_init()` 会
   `esp_ble_gattc_app_register(0)` 后 `WAIT_CB()`（`portMAX_DELAY`）等 gattc REG 事件；不注册则
   **启动永久卡死在 `[hid] boot bonds` 之前**（Task 4 冷启动实测命中）。该注册是 esp_hidh 强制初始化
   管道、即便只用 BR/EDR 也必需，不等于跑 BLE 业务流。已在 `app_main` `esp_hidh_init` 前恢复。
3. 顺带清理：删 `(void)bda;`（仓库禁 `(void)arg`）、删无用的 `#include "freertos/task.h"`、
   `g_app_mutex` 创建移到 `app_main` 最前（回调注册前）。
