# BT HID 报告解码实施计划（`apps/default/` 迭代）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 `apps/default/` 上把手柄 15 字节原始 HID 报告解成具名输入状态——先 dump/解析 Report Descriptor 确认布局，再 struct 解码、仅状态变化时打印，最后逐键实测校正映射并存档 docs。

**Architecture:** 解码逻辑独立进 `apps/default/main/hid_report.{c,h}`；`main.c` 只在 OPEN 事件调 `hid_dump_report_map()`、在 INPUT 事件调 `hid_decode()`+`hid_print_state()`。布局以 descriptor+实测为准，**不**硬套标准 Xbox（初始 struct 只是待验证假设，Task 3 实测校正）。

**Tech Stack:** ESP-IDF v6.0.2, BluedR, esp_hid（`esp_hidh_dev_report_maps_get`、`esp_hid_parse_report_map`）；board_a（`BOARD_A_TYPE=esp32-wroom-32e`，DTR 复位）；`tools/{build,burn}.py` + `tools/capture_uart.py`。

**Spec:** `docs/superpowers/specs/2026-09-05-bt-hid-report-decode-design.md`

## Global Constraints

- **不改候选算法**：`main.c` 的三层扫描/连接逻辑保持；只新增 descriptor dump + 解码 + 变化打印。
- **不动** `apps/hello_world/` / `apps/bt_scan/`；解码进 `apps/default/`（主 app），不新建 app。
- **布局靠证据**：字段偏移/字节序/符号来自 descriptor 分析 + 逐键实测；初始 Xbox-360 假设必须在 Task 3 用实测证实或纠正。
- **输出**：连接时一次性 descriptor dump（hex 分块 16B/行 + parse 报告元数据行）；运行期**仅状态变化**打印一行 `[hid] state: ...`。默认不常显原始 hex。
- **bring-up 工具**：`#define HID_DEBUG_DELTA`（定义在 `hid_report.h`）。Task 2 置 **1** 打印变化字节 `b[i]:old->new` 供 Task 3 定位；**Task 3 末归 0**（最终态默认关）。
- **API**：`esp_hidh_dev_report_maps_get(dev,&n,&maps)`；`esp_hid_parse_report_map(maps[i].data,maps[i].len)` → `esp_hid_report_map_t{reports_len,reports[]}`；`reports[i]`=`esp_hid_report_item_t{map_index,report_id,report_type,protocol_mode,usage,value_len}`；用后 `esp_hid_free_report_map()`。`esp_hid_report_type_str/usage_str/cod_major_str` 可用作字符串。
- **C 分节顺序**（AGENTS.md）：`hid_report.c/.h` 内 include→define→type→global→函数。
- **格式化闸门**：每个 `.c/.h` 改动后 `clang-format -i`；`CMakeLists.txt` 改动后 `cmake-format -c .cmake-format.yaml -i`；提交前跑 §格式化闸门命令，仓库须全绿。
- **硬件前提醒**：手柄空闲 10–30s 省电；每次采集/烧录前先确认手柄 BT 模式 + 配对（蓝灯快闪）。
- **worktree**：所有改动在 `.worktrees/bt-hid-report-decode/`（分支同名），完成后按用户指令合并。

---

## File Structure

**Create** (`bluetooth/esp32-wroom-32e/apps/default/main/`):
- `hid_report.h` — `apex5_xinput_t` + 按钮 bit 定义 + 函数声明
- `hid_report.c` — descriptor dump/解析、decode、state print（+ debug delta）

**Create** (`bluetooth/esp32-wroom-32e/docs/`):
- `apex5-hid-descriptor.md` — descriptor 原始 hex + 逐项分析 + parse 元数据 + 字节→字段表
- `apex5-hid-input-map.md` — 实测键/轴→字节/位 + 量程/符号/字节序

**Modify**:
- `apps/default/main/main.c` — OPEN 里 dump；INPUT 里 decode+变化打印（取代 M11 的常显 hex）
- `apps/default/main/CMakeLists.txt` — SRCS 加 `hid_report.c`
- `apps/default/README.md`、`AGENTS.md` — 文档同步

---

## Task 1: descriptor dump（连接时抓取报告描述符）

**Files:**
- Create: `bluetooth/esp32-wroom-32e/apps/default/main/hid_report.h`
- Create: `bluetooth/esp32-wroom-32e/apps/default/main/hid_report.c`（本任务只放 `hid_dump_report_map`）
- Modify: `bluetooth/esp32-wroom-32e/apps/default/main/main.c`（OPEN 事件调用 dump）
- Modify: `bluetooth/esp32-wroom-32e/apps/default/main/CMakeLists.txt`

**Interfaces:**
- Consumes: `esp_hidh_dev_t`（OPEN 事件的 `param->open.dev`）
- Produces: `void hid_dump_report_map(esp_hidh_dev_t *dev);`（Task 2/3 复用同一函数）

- [ ] **Step 1: 建 `hid_report.h`（声明 + include guard）**

Create `bluetooth/esp32-wroom-32e/apps/default/main/hid_report.h`:

```c
/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#ifndef BT_APP_HID_REPORT_H
#define BT_APP_HID_REPORT_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_hidh.h"

/* Dump + parse the device's HID Report Descriptor once, on connect.
 * Prints raw map bytes (16B/line) and per-report metadata via
 * esp_hid_parse_report_map (id/type/usage/len). */
void hid_dump_report_map(esp_hidh_dev_t *dev);

#endif /* BT_APP_HID_REPORT_H */
```

- [ ] **Step 2: 建 `hid_report.c`（descriptor dump）**

Create `bluetooth/esp32-wroom-32e/apps/default/main/hid_report.c`:

```c
/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * HID Report Descriptor dump + (later) input-report decode for the Apex5
 * BT path. See spec 2026-09-05-bt-hid-report-decode.
 */

#include "hid_report.h"
#include <stdio.h>
#include "esp_hid_common.h"
#include "esp_log.h"

static void dump_hex(const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        if (i % 16 == 0) {
            printf("\n[hid]  ");
        }
        printf("%02x ", data[i]);
    }
    printf("\n");
}

void hid_dump_report_map(esp_hidh_dev_t *dev) {
    size_t n_maps = 0;
    esp_hid_raw_report_map_t *maps = NULL;
    if (esp_hidh_dev_report_maps_get(dev, &n_maps, &maps) != ESP_OK) {
        ESP_LOGE("hid_report", "report_maps_get failed");
        return;
    }
    for (size_t m = 0; m < n_maps; m++) {
        printf("[hid] descriptor[%zu] len=%u bytes:", m, maps[m].len);
        dump_hex(maps[m].data, maps[m].len);

        esp_hid_report_map_t *parsed =
            esp_hid_parse_report_map(maps[m].data, maps[m].len);
        if (!parsed) {
            ESP_LOGE("hid_report", "parse_report_map failed");
            continue;
        }
        for (int i = 0; i < parsed->reports_len; i++) {
            esp_hid_report_item_t *r = &parsed->reports[i];
            printf("[hid] report: map=%u id=%u type=%s usage=%s len=%u\n",
                   r->map_index, r->report_id,
                   esp_hid_report_type_str(r->report_type),
                   esp_hid_usage_str(r->usage), r->value_len);
        }
        esp_hid_free_report_map(parsed);
    }
}
```

- [ ] **Step 3: 在 `main.c` OPEN 事件调用 dump**

In `bluetooth/esp32-wroom-32e/apps/default/main/main.c`:
1. Add `#include "hid_report.h"` with the other includes (keep `SortIncludes: false` → place after `esp_hidh_gattc.h`).
2. In `hidh_event_handler`, inside `case ESP_HIDH_OPEN_EVENT:` after the `if (param->open.status == ESP_OK) {` success branch (after printing `[hid] open:`), add:

```c
            hid_dump_report_map(param->open.dev);
```

Place the dump AFTER the existing `[hid] open: ...` printf so open log stays first.

- [ ] **Step 4: 更新 `main/CMakeLists.txt` 加 `hid_report.c`**

Edit `bluetooth/esp32-wroom-32e/apps/default/main/CMakeLists.txt`. Change the SRCS list to include the new source:

```cmake
idf_component_register(SRCS "main.c" "esp_hid_gap.c" "hid_report.c"
                       INCLUDE_DIRS ".")
```

> Keep existing `PRIV_REQUIRES`/`REQUIRES` (must still include `esp_hid`). If the current file lists `srcs`/`include_dirs` via `set()` vars, append `"hid_report.c"` there instead — match existing style.

- [ ] **Step 5: 格式化新文件**

Run:
```bash
clang-format -i bluetooth/esp32-wroom-32e/apps/default/main/hid_report.h \
                bluetooth/esp32-wroom-32e/apps/default/main/hid_report.c \
                bluetooth/esp32-wroom-32e/apps/default/main/main.c
cmake-format -c .cmake-format.yaml -i bluetooth/esp32-wroom-32e/apps/default/main/CMakeLists.txt
```

- [ ] **Step 6: build 验证**

```bash
source /opt/esp-idf/export.sh >/dev/null 2>&1
cd bluetooth/esp32-wroom-32e && python3 tools/build.py --app default 2>&1 | tail -3
```
Expected: `Project build complete.`

- [ ] **Step 7: 烧录 board_a（提醒手柄就位）**

先播提示音并让用户确认手柄 BT 模式 + 配对（见 AGENTS.md 省电提醒）。

```bash
python3 bluetooth/esp32-wroom-32e/tools/burn.py 2>&1 | tail -3
sleep 2
timeout 25 python3 tools/capture_uart.py --board-a --rst-a --duration 20 --odir /tmp --ts 2>&1 | tail -3
```
Expected in log: `[hid] descriptor[0] len=NNN bytes:` + hex lines + at least one `[hid] report: map=0 id=0 type=INPUT usage=... len=15` (or similar) + `[hid] open:`.

- [ ] **Step 8: 存 descriptor 原始输出到 docs**

Save the descriptor + report-metadata lines from the capture into a new doc:

Create `bluetooth/esp32-wroom-32e/docs/apex5-hid-descriptor.md`:

```markdown
# Apex5 BT HID Report Descriptor（Task 1 抓取）

设备：`Xbox Wireless Controller` @ `b5:5d:e7:98:54:75`（BR/EDR）

## parse 报告元数据（esp_hid_parse_report_map）

```
[paste [hid] report: ... 行]
```

## 原始 descriptor hex

```
[paste [hid] descriptor[...] + hex 行]
```
```

Then verify the file has content:
```bash
grep -c "hid" bluetooth/esp32-wroom-32e/docs/apex5-hid-descriptor.md
```
Expected: > 0.

- [ ] **Step 9: 提交**

```bash
git add bluetooth/esp32-wroom-32e/apps/default/main/hid_report.h \
        bluetooth/esp32-wroom-32e/apps/default/main/hid_report.c \
        bluetooth/esp32-wroom-32e/apps/default/main/main.c \
        bluetooth/esp32-wroom-32e/apps/default/main/CMakeLists.txt \
        bluetooth/esp32-wroom-32e/docs/apex5-hid-descriptor.md
git commit -m "feat(esp32): default app — dump+parse Apex5 HID Report Descriptor on connect

hid_dump_report_map() pulls raw report map via
esp_hidh_dev_report_maps_get + esp_hid_parse_report_map, prints hex +
per-report metadata (id/type/usage/len). Wired into OPEN_EVENT. Initial
descriptor capture saved to docs/apex5-hid-descriptor.md."
```

---

## Task 2: struct 解码 + 仅变化打印（初始布局=待验证假设）

**Files:**
- Modify: `bluetooth/esp32-wroom-32e/apps/default/main/hid_report.h`（加 `apex5_xinput_t` + 函数声明）
- Modify: `bluetooth/esp32-wroom-32e/apps/default/main/hid_report.c`（加 `hid_decode`、`hid_print_state`、debug delta）
- Modify: `bluetooth/esp32-wroom-32e/apps/default/main/main.c`（INPUT 事件改为 decode + 变化打印；移除常显 hex）

**Interfaces:**
- Consumes: `param->input.data/length`（`ESP_HIDH_INPUT_EVENT`）
- Produces:
  - `bool hid_decode(const uint8_t *buf, uint16_t len, apex5_xinput_t *out);`
  - `void hid_print_state(const apex5_xinput_t *cur);`
  - `typedef struct {...} apex5_xinput_t;`
  - `#define HID_DEBUG_DELTA` (in header, drives main.c byte-delta bring-up aid)

- [ ] **Step 1: 重写 `hid_report.h`（加结构 + 按钮位 + 声明）**

Replace the whole file `bluetooth/esp32-wroom-32e/apps/default/main/hid_report.h` with:

```c
/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#ifndef BT_APP_HID_REPORT_H
#define BT_APP_HID_REPORT_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_hidh.h"

/* Dump + parse the device's HID Report Descriptor once, on connect.
 * Prints raw map bytes (16B/line) and per-report metadata via
 * esp_hid_parse_report_map (id/type/usage/len). */
void hid_dump_report_map(esp_hidh_dev_t *dev);

/* Button bit masks in apex5_xinput_t.buttons. HYPOTHESIS from the public
 * Xbox-360 15-byte input layout; Task 3 must confirm/correct against the
 * live descriptor + per-key measurement, NOT treat these as given. */
#define BTN_DPAD_UP    0x0001
#define BTN_DPAD_DOWN  0x0002
#define BTN_DPAD_LEFT  0x0004
#define BTN_DPAD_RIGHT 0x0008
#define BTN_BACK       0x0010
#define BTN_START      0x0020
#define BTN_L3         0x0040
#define BTN_R3         0x0080
#define BTN_LB         0x0100
#define BTN_RB         0x0200
#define BTN_GUIDE      0x0400
#define BTN_A          0x1000
#define BTN_B          0x2000
#define BTN_X          0x4000
#define BTN_Y          0x8000

typedef struct {
    uint32_t buttons;        /* bitfield of BTN_* */
    uint8_t  left_trigger;   /* 0..255 hypothesis */
    uint8_t  right_trigger;  /* 0..255 hypothesis */
    int16_t  lx, ly;         /* left stick, signed, LE hypothesis */
    int16_t  rx, ry;         /* right stick, signed, LE hypothesis */
} apex5_xinput_t;

/* Debug byte-deltas for layout bring-up (Task 3). Single definition here so
 * both main.c and hid_report.c share it. Set 0 to disable (Task 3 end). */
#define HID_DEBUG_DELTA 1

bool hid_decode(const uint8_t *buf, uint16_t len, apex5_xinput_t *out);
void hid_print_state(const apex5_xinput_t *cur);

#endif /* BT_APP_HID_REPORT_H */
```

- [ ] **Step 2: `hid_report.c` 加 decode + print（放在 dump 之后，保持分节顺序：type 在 .h，此处皆函数）**

Append to `bluetooth/esp32-wroom-32e/apps/default/main/hid_report.c`:

```c
bool hid_decode(const uint8_t *buf, uint16_t len, apex5_xinput_t *out) {
    /* HYPOTHESIS layout (Xbox-360 15-byte, LE), to be corrected in Task 3:
     *   [0]      header/report-id
     *   [1..2]   buttons (16-bit LE)  -> low half of out->buttons
     *   [4]      left trigger
     *   [5]      right trigger
     *   [6..7]   lx  [8..9] ly  [10..11] rx  [12..13] ry  (int16 LE)
     */
    if (len < 15) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->buttons = (uint32_t)buf[1] | ((uint32_t)buf[2] << 8); /* buttons half-word */
    out->left_trigger = buf[4];
    out->right_trigger = buf[5];
    out->lx = (int16_t)(buf[6] | (buf[7] << 8));
    out->ly = (int16_t)(buf[8] | (buf[9] << 8));
    out->rx = (int16_t)(buf[10] | (buf[11] << 8));
    out->ry = (int16_t)(buf[12] | (buf[13] << 8));
    return true;
}

static void print_buttons(uint32_t b, char *buf, size_t n) {
    buf[0] = '\0';
    struct { uint32_t bit; const char *name; } t[] = {
        {BTN_A, "A"}, {BTN_B, "B"}, {BTN_X, "X"}, {BTN_Y, "Y"},
        {BTN_LB, "LB"}, {BTN_RB, "RB"}, {BTN_BACK, "Back"}, {BTN_START, "Start"},
        {BTN_GUIDE, "Guide"}, {BTN_L3, "L3"}, {BTN_R3, "R3"},
    };
    size_t used = 0;
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        if (b & t[i].bit) {
            int w = snprintf(buf + used, n - used, used ? "|%s" : "%s", t[i].name);
            if (w > 0) used += (size_t)w;
        }
    }
    if (used == 0) {
        snprintf(buf, n, "-");
    }
}

void hid_print_state(const apex5_xinput_t *cur) {
    char btn[64];
    print_buttons(cur->buttons, btn, sizeof(btn));
    static char d[8];
    d[0] = '\0';
    if (cur->buttons & BTN_DPAD_UP) strcat(d, "U");
    if (cur->buttons & BTN_DPAD_DOWN) strcat(d, "D");
    if (cur->buttons & BTN_DPAD_LEFT) strcat(d, "L");
    if (cur->buttons & BTN_DPAD_RIGHT) strcat(d, "R");
    printf("[hid] state: btn=%s dpad=%s lt=%u rt=%u lx=%d ly=%d rx=%d ry=%d\n",
           btn, d[0] ? d : "-", cur->left_trigger, cur->right_trigger,
           cur->lx, cur->ly, cur->rx, cur->ry);
}
```

> `<string.h>`/`<stdio.h>` already included by the includes added in Task 1.

- [ ] **Step 3: `main.c` INPUT 事件：decode + 变化才打印（带 bring-up 字节 delta），去掉常显 hex**

In `bluetooth/esp32-wroom-32e/apps/default/main/main.c`, replace the entire `ESP_HIDH_INPUT_EVENT:` case body (the M11 version printed raw hex every report) with the following single concrete block. It prints raw byte-deltas only while `HID_DEBUG_DELTA` is 1 (Task 3 bring-up aid), then decodes and prints a `[hid] state:` line **only when the decoded state changed**:

```c
    case ESP_HIDH_INPUT_EVENT: {
        apex5_xinput_t cur;
        if (!hid_decode(param->input.data, param->input.length, &cur)) {
            break;
        }
#if HID_DEBUG_DELTA
        /* bring-up aid: show which raw bytes moved, to map fields in Task 3 */
        static uint8_t raw_prev[64];
        static uint16_t raw_prev_len = 0;
        if (param->input.length <= sizeof(raw_prev)) {
            for (uint16_t i = 0; i < param->input.length; i++) {
                if (i >= raw_prev_len || raw_prev[i] != param->input.data[i]) {
                    printf("[hid] d b%u:%02x>%02x ", i,
                           i < raw_prev_len ? raw_prev[i] : 0, param->input.data[i]);
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

- [ ] **Step 4: 确保移除对 `esp_hidh_dev_bda_get`/`print_bda` 于 INPUT 分支的旧依赖**

Verify `main.c` still compiles after removing the old INPUT hex body — any now-unused local `transport`/`bda` assignments in that case are gone. Do NOT remove those helpers (still used by OPEN/CLOSE). Run `clang-format -i`.

- [ ] **Step 5: build 验证**

```bash
cd bluetooth/esp32-wroom-32e && python3 tools/build.py --app default 2>&1 | tail -3
```
Expected: `Project build complete.`

- [ ] **Step 6: 烧录 + 采集空闲基线**

(handoff: confirm controller in BT+pairing first)

```bash
python3 bluetooth/esp32-wroom-32e/tools/burn.py 2>&1 | tail -2
sleep 2
timeout 15 python3 tools/capture_uart.py --board-a --rst-a --duration 12 --odir /tmp --ts 2>&1 | tail -5
```
Expected: descriptor lines, then a single baseline `[hid] state: btn=- dpad=- lt=0 rt=0 lx=.. ly=.. rx=.. ry=..` and **no repeated state spam** while idle. (lx/ly/rx/ry center values are the hypothesis values; Task 3 interprets.)

- [ ] **Step 7: 提交**

```bash
git add bluetooth/esp32-wroom-32e/apps/default/main/
git commit -m "feat(esp32): default app — decode HID input report to named state, print on change

apex5_xinput_t (button bits + triggers + 2 sticks) + hid_decode()/
hid_print_state(); INPUT handler now decodes and prints only on state
change (memcmp), replacing M11 always-hex. Layout is a documented
Xbox-360 hypothesis; HID_DEBUG_DELTA byte-deltas enabled to drive
Task 3 empirical correction. Build + idle baseline verified on board_a."
```

---

## Task 3: 逐键/摇杆/扳机实测校正 + 产出映射表

**Files:**
- Modify: `bluetooth/esp32-wroom-32e/apps/default/main/hid_report.h` / `hid_report.c`（按实测校正偏移/位/字节序/符号；量程）
- Modify: `bluetooth/esp32-wroom-32e/apps/default/main/main.c`（若宏归属调整）
- Create: `bluetooth/esp32-wroom-32e/docs/apex5-hid-input-map.md`
- Modify: `bluetooth/esp32-wroom-32e/docs/apex5-hid-descriptor.md`（补逐字节字段表）

**Interfaces:**
- Consumes: Task 2 的 `[hid] d b<i>:old>new` delta 行 + `[hid] state:` 行
- Produces: 与实机一致的 `apex5_xinput_t`；`HID_DEBUG_DELTA` 归 0

- [ ] **Step 1: 建长采集，用户逐键操作**

(handoff: controller in BT+pairing)

```bash
timeout 90 python3 tools/capture_uart.py --board-a --duration 80 --odir /tmp --ts > /tmp/decode_live.log 2>&1 &
```
然后提示用户**按顺序**操作（每项停留 ~1s）：A、B、X、Y、LB、RB、Back、Start、Guide、L3、R3、D-pad 上/下/左/右、左摇杆上/下/左/右推满回中、右摇杆同、LT 到底回、RT 到底回。

等采集结束后停后台：
```bash
sleep 85; kill %1 2>/dev/null
```

- [ ] **Step 2: 从 delta 行提取每个控制的字节/位**

```bash
grep "\[hid\] d " /tmp/decode_live.log | head -80
```
For each control, note which `b<i>:old>new` fired. Record into `apex5-hid-input-map.md`:

Create `bluetooth/esp32-wroom-32e/docs/apex5-hid-input-map.md`:

```markdown
# Apex5 BT HID 输入映射（实测）

来源：board_a `apps/default` 逐键采集。

## 按钮 → 字节.位
| 控制 | 字节 | 位 | 观测值 |
|---|---|---|---|
| A | <b> | <bit> | <old>><new> |
| B | ... | | |
...

## 扳机 / 摇杆
| 量 | 字节 | 宽度/字节序 | 中心 | 最小 | 最大 | 符号 |
|---|---|---|---|---|---|---|
| LT | <b> | u8 | | 0 | 255 | - |
| RT | ... |
| LX | b6-7 | i16 LE | | | | |
...
```

- [ ] **Step 3: 按实测校正 `apex5_xinput_t` + `hid_decode`**

Edit `hid_report.h` button bit defines and `hid_report.c` `hid_decode()` offsets/endianness/signedness to match Step 2's observed table. Example corrections to apply **as observed** (do NOT apply blindly):
- If A fired on a different bit than 0x1000, fix `BTN_A`.
- If triggers are 2-byte or at different offsets, widen/move.
- If axis bytes are BE, or unsigned, or centered at a non-zero, adjust `hid_decode` reads + comment.

- [ ] **Step 4: 关闭 debug delta**

Set `#define HID_DEBUG_DELTA 0` in the header where it now lives.

- [ ] **Step 5: 重新格式化 + 构建**

```bash
clang-format -i bluetooth/esp32-wroom-32e/apps/default/main/hid_report.h \
                bluetooth/esp32-wroom-32e/apps/default/main/hid_report.c \
                bluetooth/esp32-wroom-32e/apps/default/main/main.c
cd bluetooth/esp32-wroom-32e && python3 tools/build.py --app default 2>&1 | tail -3
```
Expected: `Project build complete.`

- [ ] **Step 6: 实测复验（校正后，delta 关闭）**

```bash
python3 bluetooth/esp32-wroom-32e/tools/burn.py 2>&1 | tail -2
sleep 2
timeout 30 python3 tools/capture_uart.py --board-a --duration 25 --odir /tmp --ts 2>&1 | tail -3
```
Ask user to press A / move left stick / pull LT. Expected: `btn=A` / `lx,ly` 变化 / `lt` 升到端点 —— 与映射表一致，无 `d ` delta 行（已关），空闲无刷屏。

- [ ] **Step 7: 补 descriptor 文档逐字节字段表**

Append to `bluetooth/esp32-wroom-32e/docs/apex5-hid-descriptor.md` a "字段→字节偏移（实测校正后）" table cross-referencing `apex5-hid-input-map.md`, and note where it diverged from the Xbox-360 hypothesis.

- [ ] **Step 8: 提交**

```bash
git add bluetooth/esp32-wroom-32e/apps/default/main/ bluetooth/esp32-wroom-32e/docs/
git commit -m "feat(esp32): correct Apex5 HID field layout from live per-key measurement

Byte/offset/bitmap/endianness/signedness for buttons, sticks, triggers set
from the on-device byte-delta capture (not the Xbox-360 hypothesis). Adds
docs/apex5-hid-input-map.md + per-byte field table; HID_DEBUG_DELTA off;
re-verified on board_a (state lines correct, no spam, no deltas)."
```

---

## Task 4: 文档同步

**Files:**
- Modify: `bluetooth/esp32-wroom-32e/apps/default/README.md`
- Modify: `AGENTS.md`

**Interfaces:** none (docs)

- [ ] **Step 1: 更新 `apps/default/README.md`**

Append a "## HID 输入解码" section documenting: connect→descriptor dump; decode to `apex5_xinput_t`; on-change `[hid] state` output format; where the mapping table lives (`docs/apex5-hid-input-map.md`). Include a sample session excerpt.

- [ ] **Step 2: 更新 `AGENTS.md`**

In the "蓝牙方向" milestone list, add a completed line:

```markdown
- **BT HID 报告解码**（`apps/default/`：descriptor dump+parse、`apex5_xinput_t` 解码、变化打印、逐键实测映射）✓
  详见 `bluetooth/esp32-wroom-32e/docs/apex5-hid-input-map.md`
```

- [ ] **Step 3: 仓库格式化闸门**

```bash
find . \( -name '*.c' -o -name '*.h' \) -not -path './.git/*' -not -path '*/build/*' \
  -not -path './docs/reference/*' -print0 \
  | xargs -0 clang-format --dry-run --Werror
echo "clang gate exit: $?"
```
Expected: exit 0 (all formatted).

- [ ] **Step 4: 提交**

```bash
git add AGENTS.md bluetooth/esp32-wroom-32e/apps/default/README.md
git commit -m "docs: sync AGENTS.md + default README for HID report decode"
```

---

## Done (verification matrix)

| # | Step | Status |
|---|---|---|
| 1 | `build.py` (default) 编译通过 | Task 2/3 build |
| 2 | `burn.py` 烧 board_a + descriptor dump 出现 | Task 1 Step 7 |
| 3 | 空闲只一条 baseline state，无刷屏 | Task 2 Step 6 |
| 4 | 逐键 → 对应字段变化，与映射表+descriptor 一致 | Task 3 Step 6 |
| 5 | 摇杆/扳机数值随位移单调、符号/量程记录 | Task 3 Step 2/6 |
| 6 | `apex5-hid-input-map.md` + descriptor 文档落库 | Task 3 |
| 7 | `HID_DEBUG_DELTA=0`；仓库格式化闸门全绿 | Task 3/4 |

M10…(命名) 完成：Task 1+2+3+4 全过；spec §十一 达成。分支 `bt-hid-report-decode` 未合并，等用户指令。