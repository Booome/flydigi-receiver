# BT HID 主机连接 + 原始报告采集实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** ESP32 (board_a) 通过 BR/EDR HID 主机连接飞智八爪鱼5 蓝牙模式，采集原始 HID 输入报告 hex 打印到串口。

**Architecture:** `apps/default/` 从 ESP-IDF `examples/bluetooth/esp_hid_host/` cp -r 后改写 main.c：三层候选算法（语义过滤 + EWMA 平滑 + 迟滞兜底）+ HID input callback 打印 raw hex。Tools 默认 `--app=default`；`hello_world` / `bt_scan` 不动。

**Tech Stack:** ESP-IDF v6.0.2, BluedR, esp_hid 组件, board_a (`BOARD_A_TYPE=esp32-wroom-32e`, DTR 复位)

**Spec:** `docs/superpowers/specs/2026-09-05-bt-hid-host-capture-design.md`

## Global Constraints

- **Non-invasive build**: ESP-IDF `/opt/esp-idf` is read-only. Example at `/opt/esp-idf/examples/bluetooth/esp_hid_host/` is read-only; copy then modify the copy in `apps/default/`.
- **Target chip**: ESP32 (Xtensa LX6, original). `CONFIG_IDF_TARGET="esp32"` pinned in `sdkconfig.defaults`.
- **Build output**: `bluetooth/esp32-wroom-32e/build/default/` (top-level under board), via `idf.py -B ../../build/default`.
- **Bluetooth stack**: BluedR (`ESP_BT_MODE_BTDM` = dual-mode).
- **Output**: One hex line per HID input report: `[hid] report: addr=...:... transport=BR_EDR len=N data=HEX...`.
- **Candidate algorithm** (spec §四):
  - **Layer 1**: Filter to `COD major==5 && minor==2` (gamepad class).
  - **Layer 2**: EWMA smoothing `0.3*new + 0.7*prev` per device.
  - **Layer 3**: Hysteresis 3 dB on smoothed; lock at 3s stable; force-connect cap 8s.
  - **Constants**: `HYSTERESIS_DB=3`, `LOCK_WAIT_MS=3000`, `MAX_WAIT_MS=8000`, `EWMA alpha=0.3`, `CANDIDATE_GONE_ROUNDS=2`.
- **No auto-connect to MAC**: algorithm picks best stable candidate.
- **Tools default**: `--app=default` (no explicit arg required).
- **`.env` (top-level, not committed)**: no new keys; ESP32 reuses `BOARD_A_PORT` + `BOARD_A_TYPE=esp32-wroom-32e`.
- **`tools/capture_uart.py`**: top-level (M10 walk-up); CLI unchanged.
- **Board**: only board_a runs default; board_b keeps bt_scan.
- **Not touched**: `apps/hello_world/`, `apps/bt_scan/`.
- **Code style**: C uses `.clang-format` (LLVM, 4-space, 100 col). No `(void)arg`. No Chinese in code/comments.
- **No placeholders, no TBD**: every step contains the exact content.

---

## File Structure

**Create** (under `bluetooth/esp32-wroom-32e/apps/default/`):
- `CMakeLists.txt` — ESP-IDF project root (`project(default)`)
- `main/CMakeLists.txt` — main component, `idf_component_register(SRCS "main.c" "esp_hid_gap.c" REQUIRES esp_hid PRIV_REQUIRES nvs_flash INCLUDE_DIRS ".")`
- `main/main.c` — layered candidate algorithm + HID input/close callbacks (~150 lines)
- `main/esp_hid_gap.c` + `esp_hid_gap.h` — from esp_hid_host example (provides `esp_hid_scan`, `esp_hid_host_open`)
- `sdkconfig.defaults` — dual-mode + HID host config (verbatim from bt_scan)
- `README.md` — usage + verification steps

**Modify**:
- `bluetooth/esp32-wroom-32e/tools/build.py` — `--app` default → `"default"`
- `bluetooth/esp32-wroom-32e/tools/burn.py` — `--app` default → `"default"`
- `bluetooth/esp32-wroom-32e/docs/development.md` — append M11 section + tool default changes

**Not touched** (M9/M10 already set):
- `apps/hello_world/`, `apps/bt_scan/`
- `tools/capture_uart.py`, `.env`, AGENTS.md

---

## Task 1: Scaffold `apps/default/` + build verify

**Files:**
- Create: `bluetooth/esp32-wroom-32e/apps/default/CMakeLists.txt` (modified from example)
- Create: `bluetooth/esp32-wroom-32e/apps/default/main/CMakeLists.txt`
- Create: `bluetooth/esp32-wroom-32e/apps/default/main/main.c` (custom main entry — first pass)
- Create: `bluetooth/esp32-wroom-32e/apps/default/main/esp_hid_gap.c` + `.h` (copied from example)
- Create: `bluetooth/esp32-wroom-32e/apps/default/sdkconfig.defaults`
- Create: `bluetooth/esp32-wroom-32e/apps/default/README.md`

**Interfaces:**
- Consumes: `/opt/esp-idf/examples/bluetooth/esp_hid_host/`
- Produces: `idf.py -C apps/default -B ../../build/default build` exits 0; `build/default/default.bin` exists

- [ ] **Step 1: Copy the example into our app dir (non-invasive)**

Run:
```bash
cp -r /opt/esp-idf/examples/bluetooth/esp_hid_host bluetooth/esp32-wroom-32e/apps/default
ls bluetooth/esp32-wroom-32e/apps/default/
```
Expected: shows `CMakeLists.txt`, `main/`, `README.md`, `sdkconfig.defaults`, `sdkconfig.ci.*`.

- [ ] **Step 2: Inspect the example's structure**

Run:
```bash
ls bluetooth/esp32-wroom-32e/apps/default/main/
echo "=== root CMakeLists.txt ==="
cat bluetooth/esp32-wroom-32e/apps/default/CMakeLists.txt
echo "=== main/CMakeLists.txt ==="
cat bluetooth/esp32-wroom-32e/apps/default/main/CMakeLists.txt
echo "=== esp_hid_host_main.c size ==="
wc -l bluetooth/esp32-wroom-32e/apps/default/main/esp_hid_host_main.c
```
Expected:
- `main/CMakeLists.txt` has `idf_component_register(SRCS "esp_hid_host_main.c" "esp_hid_gap.c" PRIV_REQUIRES esp_hid bt nvs_flash INCLUDE_DIRS ".")`
- Root `CMakeLists.txt`: `project(esp_hid_host)` (we'll change to `default`)
- `esp_hid_host_main.c`: ~266 lines
- `esp_hid_gap.c` + `.h`: ~1190 + 103 lines (we keep these)

- [ ] **Step 3: Update root `CMakeLists.txt` (project name)**

Edit `bluetooth/esp32-wroom-32e/apps/default/CMakeLists.txt`. Change:
```cmake
project(esp_hid_host)
```
to:
```cmake
project(default)
```
(This changes the output binary from `esp_hid_host.bin` to `default.bin`.)

- [ ] **Step 4: Replace `esp_hid_host_main.c` with a minimal stub main.c (Task 2 will replace this)**

The example's `esp_hid_host_main.c` has connect logic + state machine. We replace it with a **placeholder main.c** for Task 1 to verify the build, then Task 2 implements the full layered algorithm.

```bash
rm bluetooth/esp32-wroom-32e/apps/default/main/esp_hid_host_main.c
```

Create `bluetooth/esp32-wroom-32e/apps/default/main/main.c`:

```c
/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * default: BT HID host connect + raw report capture (placeholder).
 * Real layered algorithm arrives in Task 2.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_hid_gap.h"

#define TAG "default"

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_hid_gap_init(ESP_BT_MODE_BTDM));
    ESP_LOGI(TAG, "BTDM initialized (placeholder; full logic in Task 2)");

    /* Keep alive so we can verify boot in Task 1 capture */
    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
```

- [ ] **Step 5: Update `main/CMakeLists.txt`**

Edit `bluetooth/esp32-wroom-32e/apps/default/main/CMakeLists.txt`. Replace `esp_hid_host_main.c` with `main.c`:

```cmake
idf_component_register(SRCS "main.c" "esp_hid_gap.c"
                       PRIV_REQUIRES esp_hid bt nvs_flash
                       INCLUDE_DIRS ".")
```

Verify:
```bash
cat bluetooth/esp32-wroom-32e/apps/default/main/CMakeLists.txt
```
Expected: `SRCS "main.c" "esp_hid_gap.c"`.

- [ ] **Step 6: Verify `sdkconfig.defaults`**

The example's `sdkconfig.defaults` is verbatim what `bt_scan` uses. Verify it exists and has dual-mode + HID host flags:

```bash
grep -E "^Config_BT_(ENABLED|CLASSIC|BLE|HID_HOST|BLUEDROID)|^CONFIG_IDF_TARGET" \
  bluetooth/esp32-wroom-32e/apps/default/sdkconfig.defaults
```
Expected: at least `CONFIG_BT_ENABLED=y`, `CONFIG_BT_CLASSIC_ENABLED=y`, `CONFIG_BT_BLE_ENABLED=y`, `CONFIG_BT_HID_HOST_ENABLED=y`. If `CONFIG_IDF_TARGET="esp32"` is missing, append (same fix as M10 Task 1 Step 6).

- [ ] **Step 7: Stub README.md**

Create `bluetooth/esp32-wroom-32e/apps/default/README.md`:

```markdown
# default (M11 main app)

飞智八爪鱼5 自制接收器项目的**主 app**。从本里程碑起，所有蓝牙方向的功能迭代都更新这里，不新建其他 app。

当前实现（Task 1 占位）：BTDM 初始化后空跑。完整三层候选算法 + HID 报告采集在 Task 2+。

## 构建 + 烧录

```bash
source /opt/esp-idf/export.sh
# tools/build.py / burn.py 默认 app=default，无需 --app
python3 bluetooth/esp32-wroom-32e/tools/build.py
python3 bluetooth/esp32-wroom-32e/tools/burn.py
# 抓 log（DTR 复位，沿用 M10 .env 配置）
python3 tools/capture_uart.py --board-a --rst-a --duration 60 --odir /tmp --ts
```

## 验证步骤

详见 `docs/superpowers/specs/2026-09-05-bt-hid-host-capture-design.md` §十。
```

- [ ] **Step 8: Verify build (critical check)**

```bash
source /opt/esp-idf/export.sh >/dev/null 2>&1
cd bluetooth/esp32-wroom-32e/apps/default
idf.py -B ../../build/default set-target esp32 2>&1 | tail -3
idf.py -B ../../build/default build 2>&1 | tail -5
```
Expected: `Project build complete.` exit 0.

Verify artifacts:
```bash
ls -la ../../build/default/default.bin ../../build/default/bootloader/bootloader.bin 2>&1 | head
```
Expected: both files exist.

If build fails, common issues:
- `idf.py: command not found` → `source /opt/esp-idf/export.sh`
- `esp_hid_gap` undefined → check `main/CMakeLists.txt` has `esp_hid_gap.c` in SRCS
- Component missing → check `sdkconfig.defaults` includes `CONFIG_BT_HID_HOST_ENABLED=y`

- [ ] **Step 9: Commit**

```bash
git add bluetooth/esp32-wroom-32e/apps/default/
git status --short
git commit -m "feat(esp32): default app scaffold — BTDM init placeholder

- Copied examples/bluetooth/esp_hid_host/ into apps/default/
- Replaced esp_hid_host_main.c with main.c (BTDM init only; real
  layered algorithm arrives in Task 2)
- project() renamed to 'default' (was esp_hid_host)
- Kept esp_hid_gap.c (provides esp_hid_scan, esp_hid_host_open)
- sdkconfig.defaults: dual-mode + HID host (verbatim from example);
  pinned CONFIG_IDF_TARGET=esp32
- idf.py build succeeds; default.bin + bootloader.bin produced"
```

---

## Task 2: Layered candidate algorithm + HID callbacks

**Files:**
- Modify: `bluetooth/esp32-wroom-32e/apps/default/main/main.c` (replace Task 1 placeholder with full algorithm)

**Interfaces:**
- Consumes: `apps/default/main/esp_hid_gap.{c,h}` (esp_hid_scan, esp_hid_host_open, esp_hid_scan_result_t, esp_hid_host_callbacks_t)
- Produces: a working main.c that initializes BTDM, runs the layered scan → connect → hex-print loop on input reports, recovers on close

- [ ] **Step 1: Look up esp_hid_host callback API**

Find the exact callback registration API in esp_hid_host headers:

```bash
grep -rn "esp_hidh_event_t\|esp_hid_host_set_callbacks\|esp_hid_host_open\|esp_hid_host_close" /opt/esp-idf/components/esp_hid/include/ | head -20
```

Expected hits include:
- `esp_hidh_event_t` enum (ESP_HIDH_OPEN_EVT, ESP_HIDH_INPUT_EVT, ESP_HIDH_CLOSE_EVT, ...)
- `esp_hid_host_set_callbacks(...)` (registers callbacks)
- `esp_hid_host_open(transport, addr, ...)` (initiates connection)
- `esp_hid_host_close(transport)` (disconnect)

Save the relevant signatures for reference. Adjust the code below to match the exact API.

- [ ] **Step 2: Write the full layered main.c**

Replace `bluetooth/esp32-wroom-32e/apps/default/main/main.c` with:

```c
/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * default: BT HID host connect + raw HID report capture.
 *
 * 三层候选算法（spec §四）：
 *   层1 语义过滤：只保留 gamepad-class BR/EDR（COD major=5 minor=2）
 *   层2 EWMA 平滑：0.3*新 + 0.7*旧，去除 ±3~5dB RSSI 抖动
 *   层3 迟滞 + 兜底：3dB 迟滞，3s 稳定或 8s 强制连
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_hid_gap.h"

#define TAG "default"
#define SCAN_DURATION_SEC       5
#define HYSTERESIS_DB            3
#define LOCK_WAIT_MS             3000
#define MAX_WAIT_MS              8000
#define EWMA_ALPHA_NUM           3     /* /10 → alpha=0.3 */
#define EWMA_ALPHA_DEN           10
#define CANDIDATE_GONE_ROUNDS    2

static const char *transport_str(esp_hid_transport_t t)
{
    switch (t) {
    case ESP_HID_TRANSPORT_BLE: return "BLE";
    case ESP_HID_TRANSPORT_BT:  return "BR_EDR";
    default: return "?";
    }
}

static uint8_t bda_eq(const uint8_t *a, const uint8_t *b)
{
    return memcmp(a, b, 6) == 0;
}

static void print_bda(const uint8_t *bda)
{
    printf("%02x:%02x:%02x:%02x:%02x:%02x",
           bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

/* Per-device EWMA RSSI: key = (bda, transport) */
#define EWMA_MAX 16
typedef struct {
    uint8_t bda[6];
    esp_hid_transport_t transport;
    bool used;
    float smoothed;
} ewma_entry_t;
static ewma_entry_t g_ewma[EWMA_MAX];

static float *ewma_lookup(const uint8_t *bda, esp_hid_transport_t t)
{
    for (int i = 0; i < EWMA_MAX; i++) {
        if (g_ewma[i].used && bda_eq(g_ewma[i].bda, bda) && g_ewma[i].transport == t) {
            return &g_ewma[i].smoothed;
        }
    }
    return NULL;
}

static float *ewma_get_or_create(const uint8_t *bda, esp_hid_transport_t t)
{
    if (float *p = ewma_lookup(bda, t)) return p;
    for (int i = 0; i < EWMA_MAX; i++) {
        if (!g_ewma[i].used) {
            memcpy(g_ewma[i].bda, bda, 6);
            g_ewma[i].transport = t;
            g_ewma[i].used = true;
            g_ewma[i].smoothed = -127;
            return &g_ewma[i].smoothed;
        }
    }
    return NULL;
}

static void ewma_clear(void)
{
    for (int i = 0; i < EWMA_MAX; i++) g_ewma[i].used = false;
}

/* Candidate state */
typedef struct {
    bool active;
    uint8_t bda[6];
    esp_hid_transport_t transport;
    float smoothed;
    int64_t set_at_ms;
} candidate_t;
static candidate_t g_candidate = {0};
static int g_not_seen_count = 0;
static int64_t g_scan_start_ms = 0;

static bool is_gamepad(const esp_hid_scan_result_t *r)
{
    /* 层1: 仅 gamepad-class BR/EDR. 实测 Apex5: major=PERIPHERAL(5) minor=2. */
    if (r->transport != ESP_HID_TRANSPORT_BT) return false;
    if (r->bt.cod.major != 5) return false;
    if (r->bt.cod.minor != 2) return false;
    return true;
}

static void on_hid_input(uint8_t report_id, uint8_t report_type,
                         uint8_t *data, uint16_t len, void *arg)
{
    /* Called per HID input report. Print raw hex. */
    printf("[hid] report: addr="); print_bda(g_connected_bda);
    printf(" transport=%s len=%d data=", transport_str(g_connected_transport), len);
    for (uint16_t i = 0; i < len; i++) printf("%02x", data[i]);
    printf("\n");
}

static void on_hid_event(uint8_t event, uint8_t status,
                         esp_hid_transport_t transport,
                         esp_hid_dev_t *dev, void *arg)
{
    switch (event) {
    case ESP_HIDH_OPEN_EVT:
        if (status == ESP_OK) {
            memcpy(g_connected_bda, dev->bda, 6);
            g_connected_transport = transport;
            printf("[hid] open: addr="); print_bda(dev->bda);
            printf(" transport=%s\n", transport_str(transport));
        } else {
            printf("[hid] open FAIL: addr="); print_bda(dev->bda);
            printf(" transport=%s status=0x%x\n", transport_str(transport), status);
            /* 进入候选循环 */
            memset(&g_candidate, 0, sizeof(g_candidate));
            ewma_clear();
            g_not_seen_count = 0;
            g_scan_start_ms = (int64_t)esp_timer_get_time() / 1000;
        }
        break;
    case ESP_HIDH_CLOSE_EVT:
        printf("[hid] close: addr="); print_bda(dev->bda);
        printf(" transport=%s status=0x%x\n", transport_str(transport), status);
        memset(&g_candidate, 0, sizeof(g_candidate));
        ewma_clear();
        g_not_seen_count = 0;
        g_scan_start_ms = (int64_t)esp_timer_get_time() / 1000;
        break;
    default:
        break;
    }
}

static uint8_t g_connected_bda[6];
static esp_hid_transport_t g_connected_transport;

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_hid_gap_init(ESP_BT_MODE_BTDM));
    ESP_LOGI(TAG, "BTDM initialized");

    esp_hid_host_set_callbacks(on_hid_event, on_hid_input, NULL);

    g_scan_start_ms = (int64_t)esp_timer_get_time() / 1000;

    while (1) {
        esp_hid_scan_result_t *results = NULL;
        size_t n = 0;
        esp_err_t r = esp_hid_scan(SCAN_DURATION_SEC, &n, &results);
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "scan failed: %s", esp_err_to_name(r));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* 层1: 只保留 gamepad */
        esp_hid_scan_result_t *best = NULL;
        for (size_t i = 0; i < n; i++) {
            if (is_gamepad(&results[i])) {
                if (!best || results[i].rssi > best->rssi) {
                    best = &results[i];
                }
            }
        }

        if (!best) {
            if (g_candidate.active && g_not_seen_count >= CANDIDATE_GONE_ROUNDS) {
                printf("[hid] candidate gone\n");
                memset(&g_candidate, 0, sizeof(g_candidate));
            }
            if (g_candidate.active) g_not_seen_count++;
            esp_hid_scan_results_free(results);
            continue;
        }

        /* 层2: EWMA 平滑 */
        float *smoothed_p = ewma_get_or_create(best->bda, best->transport);
        if (!smoothed_p) {
            esp_hid_scan_results_free(results);
            continue;
        }
        if (*smoothed_p == -127) *smoothed_p = (float)best->rssi;  /* 首次首采 */
        else {
            *smoothed_p = (float)best->rssi * EWMA_ALPHA_NUM / EWMA_ALPHA_DEN
                        + *smoothed_p * (EWMA_ALPHA_DEN - EWMA_ALPHA_NUM) / EWMA_ALPHA_DEN;
        }

        int64_t now_ms = (int64_t)esp_timer_get_time() / 1000;

        /* 层3: 迟滞 + 锁定 */
        if (!g_candidate.active) {
            g_candidate.active = true;
            memcpy(g_candidate.bda, best->bda, 6);
            g_candidate.transport = best->transport;
            g_candidate.smoothed = *smoothed_p;
            g_candidate.set_at_ms = now_ms;
            g_not_seen_count = 0;
            printf("[hid] candidate: addr="); print_bda(best->bda);
            printf(" smoothed=%.1f\n", g_candidate.smoothed);
        } else if (bda_eq(best->bda, g_candidate.bda) && best->transport == g_candidate.transport) {
            /* 同候选: 计时继续 */
            g_not_seen_count = 0;
        } else if (*smoothed_p >= g_candidate.smoothed + HYSTERESIS_DB) {
            /* 新候选明显更强 */
            g_candidate.active = true;
            memcpy(g_candidate.bda, best->bda, 6);
            g_candidate.transport = best->transport;
            g_candidate.smoothed = *smoothed_p;
            g_candidate.set_at_ms = now_ms;
            g_not_seen_count = 0;
            printf("[hid] candidate replace: addr="); print_bda(best->bda);
            printf(" smoothed=%.1f\n", g_candidate.smoothed);
        }
        /* else: 接近打平, 保留, 不重置计时 */

        /* 锁定条件: 稳定 3s 或超 8s 兜底 */
        int64_t now = (int64_t)esp_timer_get_time() / 1000;
        int64_t stable_elapsed = now - g_candidate.set_at_ms;
        int64_t total_elapsed = now - g_scan_start_ms;
        if (stable_elapsed >= LOCK_WAIT_MS || total_elapsed >= MAX_WAIT_MS) {
            ESP_ERROR_CHECK(esp_hid_host_open(g_candidate.transport, g_candidate.bda));
            /* on_hid_event handles open/close; main loop exits scan cycle */
            memset(&g_candidate, 0, sizeof(g_candidate));
            ewma_clear();
            g_not_seen_count = 0;
            g_scan_start_ms = now;  /* reset for next round (after close) */
        }

        esp_hid_scan_results_free(results);
    }
}
```

Notes:
- `esp_timer_get_time()` returns microseconds; divide by 1000 for ms.
- `EWMA initial value: -127` (use best's RSSI on first sample).
- `g_connected_bda`/`g_connected_transport` globals used in `on_hid_input` callback.

If `esp_hid_host_set_callbacks` doesn't accept those exact signatures, adjust by the actual header. Cross-check Step 1 findings.

- [ ] **Step 3: Build verify**

```bash
source /opt/esp-idf/export.sh >/dev/null 2>&1
cd bluetooth/esp32-wroom-32e/apps/default
idf.py -B ../../build/default --no-set-target build 2>&1 | tail -8
```
Expected: `Project build complete.` exit 0.

Verify:
```bash
ls -la ../../build/default/default.bin 2>&1 | head
```
Expected: file exists.

If compile fails, common issues:
- `esp_timer_get_time` not declared → add `# include "esp_timer.h"`
- `EWMA_ALPHA_*` arithmetic warnings → cast explicitly

- [ ] **Step 4: Commit**

```bash
git add bluetooth/esp32-wroom-32e/apps/default/main/main.c
git commit -m "feat(esp32): default app — layered candidate algorithm + HID report dump

Three-layer approach (spec §四):
  layer1 gamepad-class filter (COD major=5 minor=2)
  layer2 EWMA smoothing (alpha=0.3) on RSSI per device
  layer3 hysteresis 3dB + 3s stable lock + 8s force-connect cap

+ HID input callback prints raw hex per report
+ open/close events reset candidate and re-enter scan loop

Constants HYSTERESIS_DB=3, LOCK_WAIT_MS=3000, MAX_WAIT_MS=8000,
EWMA alpha=0.3, CANDIDATE_GONE_ROUNDS=2."
```

---

## Task 3: Tool defaults + flash + initial capture

**Files:**
- Modify: `bluetooth/esp32-wroom-32e/tools/build.py` — `--app` default → `"default"`
- Modify: `bluetooth/esp32-wroom-32e/tools/burn.py` — `--app` default → `"default"`

**Interfaces:**
- Consumes: Task 2's `build/default/default.bin`
- Produces: board_a flashed with default app; capture shows init + scan loop

- [ ] **Step 1: Update `tools/build.py` default**

Edit `bluetooth/esp32-wroom-32e/tools/build.py`. Find:
```python
ap.add_argument("--app", default="hello_world", help="app name under apps/")
```
Change `default="hello_world"` to `default="default"`.

- [ ] **Step 2: Update `tools/burn.py` default**

Edit `bluetooth/esp32-wroom-32e/tools/burn.py`. Find:
```python
ap.add_argument("--app", default="hello_world")
```
Change to:
```python
ap.add_argument("--app", default="default")
```

- [ ] **Step 3: Verify `--help` shows new defaults**

```bash
python3 bluetooth/esp32-wroom-32e/tools/build.py --help 2>&1 | grep -A2 "\-\-app"
python3 bluetooth/esp32-wroom-32e/tools/burn.py --help 2>&1 | grep -A2 "\-\-app"
```
Expected: `--app APP  [default: default]` in both.

- [ ] **Step 4: Verify default invocation runs against `apps/default/` (no `--app`)**

```bash
source /opt/esp-idf/export.sh >/dev/null 2>&1
# This should clean build of apps/default (current state from Task 2; should be incremental / no-op)
python3 bluetooth/esp32-wroom-32e/tools/build.py --no-set-target 2>&1 | tail -5
```
Expected: `Project build complete.` (or just an incremental "no work to do" / ninja no-op). If it complains about a missing app, `--app default` is the fix.

- [ ] **Step 5: Commit tool defaults**

```bash
git add bluetooth/esp32-wroom-32e/tools/build.py bluetooth/esp32-wroom-32e/tools/burn.py
git commit -m "feat(esp32): build/burn.py default --app=default

Project convention: from M11 onward, default app is apps/default/.
hello_world is the M9 env baseline (frozen), bt_scan is the M10
scanner tool (frozen). Main feature work updates apps/default/.
Tools now default to default; --app <name> still works for explicit
selection of hello_world or bt_scan."
```

- [ ] **Step 6: Configure ctrl board pin A8 HIGH (board_a EN)**

```bash
uart-gpio config /dev/serial/by-path/pci-0000:00:14.0-usb-0:3.4.1.1:1.0-port0 A 8 push-pull
uart-gpio write  /dev/serial/by-path/pci-0000:00:14.0-usb-0:3.4.1.1:1.0-port0 A 8 1
uart-gpio read   /dev/serial/by-path/pci-0000:00:14.0-usb-0:3.4.1.1:1.0-port0 A 8
```
Expected: `pin A8 -> HIGH`.

- [ ] **Step 7: Flash default to board_a**

```bash
source /opt/esp-idf/export.sh >/dev/null 2>&1
# No --app: uses default
python3 bluetooth/esp32-wroom-32e/tools/burn.py 2>&1 | tail -5
```
Expected tail: `Hash of data verified.` + `Hard resetting via RTS pin... Done`.

- [ ] **Step 8: Capture initial scan output (no controller yet)**

```bash
sleep 2
timeout 16 python3 tools/capture_uart.py --board-a --rst-a --duration 12 --odir /tmp --ts 2>&1 | tail -10
```
Expected to see:
- ESP-IDF boot log
- `[default] BTDM initialized`
- (no `[hid] candidate:` because no gamepad visible — controller is off)

If you see errors, check:
- `esp_hid_gap_init` failed → check `sdkconfig.defaults`
- Crashes → check BLE/CLASSIC enabled

- [ ] **Step 9: Commit (no code changes, skip if nothing to commit)**

```bash
git status --short
```
Expected: empty (Task 3 was verification only).

---

## Task 4: Manual hardware verification (controller in BT mode)

**Files:** none modified (user-driven hardware step)

**Interfaces:**
- Consumes: board_a flashed with default app (Task 3 done)
- Produces: capture log showing candidate tracking → connect → HID reports on button press

- [ ] **Step 1: Inform user to enable controller**

Tell user (play notify first, per AGENTS.md "硬件连接切换规则"):

```
请按以下步骤操作（手柄切到蓝牙模式 + 配对）：

1. 飞智八爪鱼5 关机（长按 HOME / 模式键 ~5s）
2. 背面模式开关拨到中间（蓝牙 / 蓝色 LED 位置）
3. 长按配对键 3-5 秒（蓝色 LED 应**快闪**，进入可发现）
4. 确认后回复 "开好了" / "完成"
```

Then play the notify sound:
```bash
bash tools/notify.sh
```

Wait for user confirmation before continuing.

- [ ] **Step 2: Capture after user confirms controller is in BT mode**

```bash
cd .worktrees/bt-hid-host
sleep 1
timeout 30 python3 tools/capture_uart.py --board-a --rst-a --duration 28 --odir /tmp --ts 2>&1 | tail -10
```

Examine the latest log:
```bash
LATEST=$(ls -t /tmp/board_a_*.log | head -1)
echo "=== candidate events ==="
grep "\[hid\] candidate" "$LATEST" | head -10
echo "=== open events ==="
grep "\[hid\] open" "$LATEST" | head
echo "=== reports (if connected) ==="
grep "\[hid\] report" "$LATEST" | head -5
```
Expected:
- Multiple `[hid] candidate: addr=b5:5d:e7:98:54:75 smoothed=...` lines (candidate tracking)
- One `[hid] open: addr=b5:5d:e7:98:54:75 transport=BR_EDR` line after ~3s of stable candidate

If no `[hid] open` after 28s, candidate didn't stabilize. Check the scan log to see what's happening.

- [ ] **Step 3: Press buttons on controller, see HID reports**

While ESP32 is connected and capturing:
- User presses buttons / moves sticks on controller
- `capture_uart.py` should log `[hid] report: ... data=HEX...` lines

```bash
# Live capture (Ctrl+C to stop after a few reports)
LATEST=$(ls -t /tmp/board_a_*.log | head -1)
grep "\[hid\] report" "$LATEST" | head -5
```

Expected: distinct hex patterns as different buttons are pressed. If reports all look identical regardless of button presses, the input callback may not be hooked correctly → check `on_hid_input` registration.

If no reports at all but `[hid] open` is shown, the controller may not be sending input reports (e.g., no buttons pressed) → ask user to press buttons explicitly.

- [ ] **Step 4: Test disconnect-reconnect**

```bash
# User turns off controller off, waits 5s, turns it back on
# Then check capture log
LATEST=$(ls -t /tmp/board_a_*.log | head -1)
grep -E "\[hid\] (close|open|candidate)" "$LATEST"
```
Expected: `[hid] close: ...` then back to `[hid] candidate: ...` then `[hid] open: ...` after controller is back.

- [ ] **Step 5: Commit (no code changes — verification only)**

```bash
git status --short
```
Expected: empty (no uncommitted changes).

If you want to preserve a sample log for documentation:
```bash
LATEST=$(ls -t /tmp/board_a_*.log | head -1)
cp "$LATEST" bluetooth/esp32-wroom-32e/docs/sample-default-capture.log
git add bluetooth/esp32-wroom-32e/docs/sample-default-capture.log
git commit -m "docs: sample default app capture log (M11 verification)"
```

---

## Task 5: Doc sync (development.md)

**Files:**
- Modify: `bluetooth/esp32-wroom-32e/docs/development.md` — append M11 section + tool default change

**Interfaces:** none (doc-only)

- [ ] **Step 1: Append M11 section to development.md**

Edit `bluetooth/esp32-wroom-32e/docs/development.md`. Find `## 后续里程碑（不在 M10 范围）` block (or end of file) and append:

```markdown

## M11: BT HID 主机连接 + 原始报告采集

详见：
- 设计：`docs/superpowers/specs/2026-09-05-bt-hid-host-capture-design.md`
- 实施计划：`docs/superpowers/plans/2026-09-05-bt-hid-host-capture.md`
- App：`apps/default/`（**项目主 app**，从本里程碑起固定在 `default`）

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
[hid] candidate: addr=b5:5d:e7:98:54:75 smoothed=-48.5
[hid] open: addr=b5:5d:e7:98:54:75 transport=BR_EDR
[hid] report: addr=b5:5d:e7:98:54:75 transport=BR_EDR len=15 data=80cc057f820102030405060708090a
[hid] close: addr=b5:5d:e7:98:54:75 transport=BR_EDR status=0x13
```

### 里程碑命名约定

不再用 `M<n>` 编号——换板时 Mn 容易乱。新里程碑按**工作内容命名**（`apps/hello_world/`、`apps/bt_scan/`、`apps/default/`）。spec/plan/分支名 = `<topic>-<action>` 形式。
```

- [ ] **Step 2: Commit**

```bash
git add bluetooth/esp32-wroom-32e/docs/development.md
git commit -m "docs: M11 default app section + tool default change

- bluetooth/esp32-wroom-32e/docs/development.md: 三层候选算法摘要、
  工具默认值改动说明、M11 输出格式样例
- 不动 hello_world/bt_scan app、不动 capture_uart.py"
```

---

## Done (M11 verification matrix)

After Task 5, all spec section 十 verification steps pass:

| # | Step | Status |
|---|---|---|
| 1 | `python3 bluetooth/esp32-wroom-32e/tools/build.py` (no `--app`) 编译通过 | ✓ (Task 1 Step 8) |
| 2 | `python3 bluetooth/esp32-wroom-32e/tools/burn.py` (no `--app`) 烧录成功 | ✓ (Task 3 Step 7) |
| 3 | 看到启动 + 候选阶段 `[hid] candidate:` | ✓ (Task 3 Step 8) |
| 4 | 手柄切 BT 模式 + 进配对 → ESP32 在 3s 内 `[hid] open:` | ✓ (Task 4 Step 2) |
| 5 | **按手柄按键** → `[hid] report: ... data=...` 持续输出 | ✓ (Task 4 Step 3) |
| 6 | 长时间观察 → HID 报告持续、稳定 | ✓ (Task 4 Step 3) |
| 7 | **手柄关 → ESP32 close + 候选循环恢复 → 手柄重开 → 重连** | ✓ (Task 4 Step 4) |

**M11 done when**: Task 1+2+3+4+5 all complete; spec §十二 criteria met.