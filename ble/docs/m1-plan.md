# M1 实现计划：USB CDC 后端 + 文本格式化 + 模拟数据

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 nRF52840 Dongle 上实现模拟手柄数据的文本格式化输出，通过 USB CDC 串口发送到 PC。

**Architecture:** 分层设计 -- controller_state.h 数据结构 -> formatter_text.c 文本格式化 -> output_cdc.c USB CDC 后端。纯逻辑层用 native_sim + ztest 在 PC 上单元测试，硬件层在 Dongle 上集成验证。

**Tech Stack:** Zephyr RTOS (NCS v3.4.0), C, ztest, native_sim, USB CDC ACM (USB_DEVICE_STACK_NEXT)

## Global Constraints

- NCS v3.4.0, Zephyr SDK 14.3.0, arm-zephyr-eabi-gcc 14.3.0
- Board: `nrf52840dongle` (not `nrf52840dongle_nrf52840`)
- 编译必须设置 `ZEPHYR_BASE=~/ncs/v3.4.0/zephyr`（项目不在 NCS workspace 内）
- 编译通过 `nrfutil sdk-manager toolchain launch --ncs-version v3.4.0 --` 隔离环境
- 烧录需要 DFU zip（`nrfutil nrf5sdk-tools pkg generate` + `nrfutil device program`）
- 代码和注释用英文，文档用中文
- 使用 git worktree 隔离开发

## File Structure

| 文件 | 职责 |
|------|------|
| `ble-receiver/firmware/src/controller_state.h` | 手柄状态数据结构 + 按钮宏 |
| `ble-receiver/firmware/src/formatter_text.h` | 文本格式化器接口 |
| `ble-receiver/firmware/src/formatter_text.c` | 文本格式化器实现 |
| `ble-receiver/firmware/src/output.h` | 统一输出接口 |
| `ble-receiver/firmware/src/output_cdc.c` | USB CDC 后端实现 |
| `ble-receiver/firmware/src/main.c` | 入口 + 模拟数据主循环 |
| `ble-receiver/firmware/CMakeLists.txt` | 固件构建配置 |
| `ble-receiver/firmware/prj.conf` | 固件 Kconfig 配置 |
| `ble-receiver/firmware/Kconfig` | 自定义 Kconfig 配置项 |
| `ble-receiver/tests/unit/test_formatter_text/CMakeLists.txt` | 测试构建配置 |
| `ble-receiver/tests/unit/test_formatter_text/prj.conf` | 测试 Kconfig 配置 |
| `ble-receiver/tests/unit/test_formatter_text/testcase.yaml` | Twister 测试配置 |
| `ble-receiver/tests/unit/test_formatter_text/src/test_formatter_text.c` | 格式化器单元测试 |
| `Makefile` | 添加固件和测试 targets (修改) |

---

### Task 1: 项目骨架 + controller_state.h

**Files:**
- Create: `ble-receiver/firmware/src/controller_state.h`
- Create: `ble-receiver/firmware/src/main.c` (空 main)
- Create: `ble-receiver/firmware/CMakeLists.txt`
- Create: `ble-receiver/firmware/prj.conf`
- Create: `ble-receiver/firmware/Kconfig`

**Interfaces:**
- Produces: `struct controller_state`, `BTN_*` macros (used by Tasks 2, 4)

- [ ] **Step 1: 创建 controller_state.h**

```c
#ifndef CONTROLLER_STATE_H
#define CONTROLLER_STATE_H

#include <stdint.h>
#include <zephyr/sys/util.h>

/* Button bitmask definitions */
#define BTN_A       BIT(0)
#define BTN_B       BIT(1)
#define BTN_X       BIT(2)
#define BTN_Y       BIT(3)
#define BTN_LB      BIT(4)
#define BTN_RB      BIT(5)
#define BTN_BACK    BIT(6)
#define BTN_START   BIT(7)
#define BTN_GUIDE   BIT(8)
#define BTN_L3      BIT(9)
#define BTN_R3      BIT(10)
#define BTN_DUP     BIT(11)
#define BTN_DDOWN   BIT(12)
#define BTN_DLEFT   BIT(13)
#define BTN_DRIGHT  BIT(14)
/* bit 15 reserved */

/* Structured controller state - standard interface between layers */
struct controller_state {
    uint16_t buttons;      /* Button bitmask (see BTN_* above) */
    uint8_t  lt;           /* Left trigger  0-255 */
    uint8_t  rt;           /* Right trigger 0-255 */
    int16_t  lx;           /* Left stick X  -32768 ~ 32767 */
    int16_t  ly;           /* Left stick Y */
    int16_t  rx;           /* Right stick X */
    int16_t  ry;           /* Right stick Y */
    uint8_t  battery;      /* Battery level 0-100 */
};

#endif /* CONTROLLER_STATE_H */
```

- [ ] **Step 2: 创建 main.c (空 main)**

```c
#include <zephyr/kernel.h>

int main(void)
{
    printk("BLE Receiver M1\n");
    return 0;
}
```

- [ ] **Step 3: 创建 Kconfig**

```kconfig
# Copyright (c) 2024 Flydigi Receiver Project
# SPDX-License-Identifier: Apache-2.0

choice OUTPUT_FORMAT
    prompt "Output data format"
    default OUTPUT_FORMAT_TEXT

config OUTPUT_FORMAT_TEXT
    bool "Text format (human-readable)"

config OUTPUT_FORMAT_BINARY
    bool "Binary format (16-byte frame)"
endchoice

choice OUTPUT_BACKEND
    prompt "Output backend"
    default OUTPUT_CDC

config OUTPUT_CDC
    bool "USB CDC ACM"

config OUTPUT_UART
    bool "UART TTL"

config OUTPUT_HID
    bool "USB HID (transparent passthrough)"
endchoice
```

- [ ] **Step 4: 创建 prj.conf**

```
# Output configuration
CONFIG_OUTPUT_FORMAT_TEXT=y
CONFIG_OUTPUT_CDC=y

# USB CDC ACM (new USB stack)
CONFIG_USB_DEVICE_STACK_NEXT=y
CONFIG_USBD_CDC_ACM_CLASS=y
CONFIG_SERIAL=y
CONFIG_UART_LINE_CTRL=y
CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT=n

# Logging
CONFIG_LOG=y
CONFIG_USB_LOG_LEVEL_ERR=y
CONFIG_USBD_LOG_LEVEL_ERR=y

# Sample USB device descriptors
CONFIG_SAMPLE_USBD_PID=0x0001
CONFIG_SAMPLE_USBD_PRODUCT="Flydigi BLE Receiver"
```

- [ ] **Step 5: 创建 CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ble_receiver)

# Reuse Zephyr sample USB device initialization code
include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
target_include_directories(app PRIVATE src)
```

- [ ] **Step 6: 验证编译**

Run: `make build-fw` (需先在 Makefile 中添加 target，或手动运行)
```bash
ZEPHYR_BASE=~/ncs/v3.4.0/zephyr nrfutil sdk-manager toolchain launch --ncs-version v3.4.0 -- \
  west build -b nrf52840dongle ble-receiver/firmware -d build-fw --no-sysbuild
```
Expected: 编译成功，生成 `build-fw/zephyr/zephyr.elf`

- [ ] **Step 7: Commit**

```bash
git add ble-receiver/firmware/
git commit -m "feat: project skeleton with controller_state and Kconfig"
```

---

### Task 2: formatter_text TDD (测试 + 实现)

**Files:**
- Create: `ble-receiver/firmware/src/formatter_text.h`
- Create: `ble-receiver/firmware/src/formatter_text.c`
- Create: `ble-receiver/tests/unit/test_formatter_text/CMakeLists.txt`
- Create: `ble-receiver/tests/unit/test_formatter_text/prj.conf`
- Create: `ble-receiver/tests/unit/test_formatter_text/testcase.yaml`
- Create: `ble-receiver/tests/unit/test_formatter_text/src/test_formatter_text.c`

**Interfaces:**
- Consumes: `struct controller_state`, `BTN_*` (from Task 1)
- Produces: `formatter_text_format()`, `FORMATTER_TEXT_MAX_LEN` (used by Task 4)

- [ ] **Step 1: 创建 formatter_text.h**

```c
#ifndef FORMATTER_TEXT_H
#define FORMATTER_TEXT_H

#include "controller_state.h"
#include <stddef.h>

#define FORMATTER_TEXT_MAX_LEN 65  /* actual output is 64 bytes + null */

/*
 * Format controller_state into a text line.
 * Format: LX:%06d,LY:%06d,RX:%06d,RY:%06d,BTN:%04x,LT:%03d,RT:%03d\r\n
 * Returns bytes written (excluding null terminator).
 * Returns 0 if buf_size is too small.
 * Actual output length is always 64 bytes.
 */
size_t formatter_text_format(const struct controller_state *state,
                             char *buf, size_t buf_size);

#endif /* FORMATTER_TEXT_H */
```

- [ ] **Step 2: 创建测试目录结构和构建文件**

`tests/unit/test_formatter_text/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_formatter_text)

target_sources(app PRIVATE
    src/test_formatter_text.c
    ../../../firmware/src/formatter_text.c
)
target_include_directories(app PRIVATE ../../../firmware/src)
```

`tests/unit/test_formatter_text/prj.conf`:
```
CONFIG_ZTEST=y
```

`tests/unit/test_formatter_text/testcase.yaml`:
```yaml
tests:
  ble_receiver.test_formatter_text:
    platform_allow:
      - native_sim
    integration_platforms:
      - native_sim
    tags:
      - formatter
      - unit
```

- [ ] **Step 3: 创建测试文件 test_formatter_text.c**

```c
#include <zephyr/ztest.h>
#include <string.h>
#include "controller_state.h"
#include "formatter_text.h"

ZTEST_SUITE(formatter_text_tests, NULL, NULL, NULL, NULL, NULL);

/* Expected output for zero state (64 bytes):
 * LX:000000,LY:000000,RX:000000,RY:000000,BTN:0000,LT:000,RT:000\r\n
 */
ZTEST(formatter_text_tests, test_zero_state)
{
    struct controller_state state = {0};
    char buf[FORMATTER_TEXT_MAX_LEN];
    size_t len = formatter_text_format(&state, buf, sizeof(buf));

    zassert_equal(len, 64, "expected 64, got %zu", len);
    zassert_str_equal(buf, "LX:000000,LY:000000,RX:000000,RY:000000,BTN:0000,LT:000,RT:000\r\n");
}

ZTEST(formatter_text_tests, test_stick_max)
{
    struct controller_state state = {
        .lx = 32767, .ly = 32767, .rx = 32767, .ry = 32767,
    };
    char buf[FORMATTER_TEXT_MAX_LEN];
    size_t len = formatter_text_format(&state, buf, sizeof(buf));

    zassert_equal(len, 64, "expected 64, got %zu", len);
    zassert_str_equal(buf, "LX:032767,LY:032767,RX:032767,RY:032767,BTN:0000,LT:000,RT:000\r\n");
}

ZTEST(formatter_text_tests, test_stick_min)
{
    struct controller_state state = {
        .lx = -32768, .ly = -32768, .rx = -32768, .ry = -32768,
    };
    char buf[FORMATTER_TEXT_MAX_LEN];
    size_t len = formatter_text_format(&state, buf, sizeof(buf));

    zassert_equal(len, 64, "expected 64, got %zu", len);
    zassert_str_equal(buf, "LX:-32768,LY:-32768,RX:-32768,RY:-32768,BTN:0000,LT:000,RT:000\r\n");
}

ZTEST(formatter_text_tests, test_trigger_max)
{
    struct controller_state state = { .lt = 255, .rt = 255 };
    char buf[FORMATTER_TEXT_MAX_LEN];
    formatter_text_format(&state, buf, sizeof(buf));

    zassert_str_equal(buf, "LX:000000,LY:000000,RX:000000,RY:000000,BTN:0000,LT:255,RT:255\r\n");
}

ZTEST(formatter_text_tests, test_trigger_mid)
{
    struct controller_state state = { .lt = 128, .rt = 128 };
    char buf[FORMATTER_TEXT_MAX_LEN];
    formatter_text_format(&state, buf, sizeof(buf));

    zassert_str_equal(buf, "LX:000000,LY:000000,RX:000000,RY:000000,BTN:0000,LT:128,RT:128\r\n");
}

ZTEST(formatter_text_tests, test_buttons_all)
{
    struct controller_state state = { .buttons = 0xFFFF };
    char buf[FORMATTER_TEXT_MAX_LEN];
    formatter_text_format(&state, buf, sizeof(buf));

    /* Check BTN field is ffff */
    zassert_mem_equal(buf + 40, "BTN:ffff", 8);
}

ZTEST(formatter_text_tests, test_buttons_single)
{
    char buf[FORMATTER_TEXT_MAX_LEN];
    struct controller_state state = {0};

    for (int i = 0; i < 15; i++) {
        state.buttons = BIT(i);
        formatter_text_format(&state, buf, sizeof(buf));
        char expected[9];
        snprintf(expected, sizeof(expected), "BTN:%04x", BIT(i));
        zassert_mem_equal(buf + 40, expected, 8,
                          "button bit %d failed", i);
    }
}

ZTEST(formatter_text_tests, test_format_stable)
{
    struct controller_state state = {
        .lx = 12345, .ly = -5432, .rx = 32767, .ry = -32768,
        .buttons = 0x0F0F, .lt = 200, .rt = 50,
    };
    char buf1[FORMATTER_TEXT_MAX_LEN];
    char buf2[FORMATTER_TEXT_MAX_LEN];
    size_t len1, len2;

    for (int i = 0; i < 10; i++) {
        len1 = formatter_text_format(&state, buf1, sizeof(buf1));
        len2 = formatter_text_format(&state, buf2, sizeof(buf2));
        zassert_equal(len1, len2, "length changed on iteration %d", i);
        zassert_mem_equal(buf1, buf2, len1, "output changed on iteration %d", i);
    }
}

ZTEST(formatter_text_tests, test_crlf)
{
    struct controller_state state = {0};
    char buf[FORMATTER_TEXT_MAX_LEN];
    size_t len = formatter_text_format(&state, buf, sizeof(buf));

    zassert_equal(buf[len - 2], '\r', "expected \\r before end");
    zassert_equal(buf[len - 1], '\n', "expected \\n at end");
}

ZTEST(formatter_text_tests, test_buffer_overflow)
{
    struct controller_state state = {0};
    char buf[10];
    size_t len = formatter_text_format(&state, buf, sizeof(buf));

    zassert_equal(len, 0, "expected 0 for small buffer, got %zu", len);
}
```

- [ ] **Step 4: 构建测试，验证编译失败 (formatter_text.c 不存在)**

```bash
ZEPHYR_BASE=~/ncs/v3.4.0/zephyr nrfutil sdk-manager toolchain launch --ncs-version v3.4.0 -- \
  west build -b native_sim ble-receiver/tests/unit/test_formatter_text -d build-test --no-sysbuild
```
Expected: 编译失败，找不到 `formatter_text.c` 或 `formatter_text_format` 符号

- [ ] **Step 5: 创建 formatter_text.c 实现**

```c
#include "formatter_text.h"
#include <stdio.h>

/*
 * Format: LX:%06d,LY:%06d,RX:%06d,RY:%06d,BTN:%04x,LT:%03d,RT:%03d\r\n
 * Max output: 60 bytes (excluding null)
 */
size_t formatter_text_format(const struct controller_state *state,
                             char *buf, size_t buf_size)
{
    if (buf_size < FORMATTER_TEXT_MAX_LEN) {
        return 0;
    }

    int len = snprintf(buf, buf_size,
        "LX:%06d,LY:%06d,RX:%06d,RY:%06d,BTN:%04x,LT:%03d,RT:%03d\r\n",
        state->lx, state->ly, state->rx, state->ry,
        state->buttons, state->lt, state->rt);

    if (len < 0) {
        return 0;
    }

    return (size_t)len;
}
```

- [ ] **Step 6: 构建并运行测试**

```bash
ZEPHYR_BASE=~/ncs/v3.4.0/zephyr nrfutil sdk-manager toolchain launch --ncs-version v3.4.0 -- \
  west build -b native_sim ble-receiver/tests/unit/test_formatter_text -d build-test --no-sysbuild
ZEPHYR_BASE=~/ncs/v3.4.0/zephyr nrfutil sdk-manager toolchain launch --ncs-version v3.4.0 -- \
  west build -t run -d build-test
```
Expected: 10 个测试全部 PASS

- [ ] **Step 7: Commit**

```bash
git add ble-receiver/firmware/src/formatter_text.h ble-receiver/firmware/src/formatter_text.c
git add ble-receiver/tests/unit/test_formatter_text/
git commit -m "feat: text formatter with ztest unit tests"
```

---

### Task 3: output 接口 + USB CDC 后端

**Files:**
- Create: `ble-receiver/firmware/src/output.h`
- Create: `ble-receiver/firmware/src/output_cdc.c`

**Interfaces:**
- Produces: `output_init()`, `output_send()` (used by Task 4)

- [ ] **Step 1: 创建 output.h**

```c
#ifndef OUTPUT_H
#define OUTPUT_H

#include <stddef.h>
#include <stdint.h>

/*
 * Initialize the output backend.
 * Returns 0 on success, negative errno on failure.
 */
int output_init(void);

/*
 * Send data through the output backend.
 * Returns 0 on success (data is discarded if backend not ready).
 * Returns negative errno on error.
 */
int output_send(const uint8_t *data, size_t len);

#endif /* OUTPUT_H */
```

- [ ] **Step 2: 创建 output_cdc.c**

```c
#include "output.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>
#include <sample_usbd.h>

LOG_MODULE_REGISTER(output_cdc, LOG_LEVEL_INF);

static const struct device *const cdc_dev =
    DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
static struct usbd_context *usbd_ctx;
static bool cdc_ready;
static K_SEM_DEFINE(dtr_sem, 0, 1);

static void on_usbd_msg(struct usbd_context *const ctx,
                        const struct usbd_msg *msg)
{
    if (msg->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
        uint32_t dtr = 0;
        uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_DTR, &dtr);
        if (dtr) {
            cdc_ready = true;
            k_sem_give(&dtr_sem);
            LOG_INF("DTR set, CDC ready");
        } else {
            cdc_ready = false;
            LOG_INF("DTR cleared, CDC not ready");
        }
    }
}

int output_init(void)
{
    if (!device_is_ready(cdc_dev)) {
        LOG_ERR("CDC ACM device not ready");
        return -ENODEV;
    }

    usbd_ctx = sample_usbd_init_device(on_usbd_msg);
    if (usbd_ctx == NULL) {
        LOG_ERR("Failed to init USB device");
        return -ENODEV;
    }

    int err = usbd_enable(usbd_ctx);
    if (err) {
        LOG_ERR("Failed to enable USB (err %d)", err);
        return err;
    }

    LOG_INF("USB CDC initialized, waiting for DTR");
    return 0;
}

int output_send(const uint8_t *data, size_t len)
{
    if (!cdc_ready) {
        return 0;
    }

    for (size_t i = 0; i < len; i++) {
        uart_poll_out(cdc_dev, data[i]);
    }
    return 0;
}
```

- [ ] **Step 3: 验证编译**

```bash
ZEPHYR_BASE=~/ncs/v3.4.0/zephyr nrfutil sdk-manager toolchain launch --ncs-version v3.4.0 -- \
  west build -b nrf52840dongle ble-receiver/firmware -d build-fw --no-sysbuild
```
Expected: 编译成功（main.c 仍是空 main，output_cdc.c 被编译但未调用）

- [ ] **Step 4: Commit**

```bash
git add ble-receiver/firmware/src/output.h ble-receiver/firmware/src/output_cdc.c
git commit -m "feat: USB CDC output backend"
```

---

### Task 4: main.c 模拟数据

**Files:**
- Modify: `ble-receiver/firmware/src/main.c`

**Interfaces:**
- Consumes: `output_init()`, `output_send()` (Task 3), `formatter_text_format()` (Task 2), `struct controller_state` (Task 1)

- [ ] **Step 1: 更新 main.c**

```c
#include <zephyr/kernel.h>
#include "controller_state.h"
#include "formatter_text.h"
#include "output.h"

#define SIM_INTERVAL_MS 500

static void sim_update(struct controller_state *s)
{
    static int16_t stick = 0;
    static uint8_t trig = 0;
    static uint16_t btn = 0;
    static int tick = 0;

    /* Sticks: step 4096, wraps through full int16_t range in 16 steps */
    stick += 4096;
    s->lx = stick;
    s->ly = -stick;
    s->rx = stick / 2;
    s->ry = -stick / 2;

    /* Triggers: step 32, wraps 0-255 in 8 steps */
    trig += 32;
    s->lt = trig;
    s->rt = 255 - trig;

    /* Buttons: rotate one bit every 4 ticks (bit 0 -> bit 14) */
    if (++tick % 4 == 0) {
        if (btn == 0) {
            btn = 1;
        } else {
            btn = (btn << 1) | (btn >> 15);
        }
    }
    s->buttons = btn;
}

int main(void)
{
    printk("BLE Receiver M1 - simulated data\n");

    output_init();

    struct controller_state state = {0};

    while (1) {
        sim_update(&state);

#if IS_ENABLED(CONFIG_OUTPUT_FORMAT_TEXT)
        char buf[FORMATTER_TEXT_MAX_LEN];
        size_t len = formatter_text_format(&state, buf, sizeof(buf));
        output_send((const uint8_t *)buf, len);
#endif

        k_sleep(K_MSEC(SIM_INTERVAL_MS));
    }

    return 0;
}
```

- [ ] **Step 2: 验证编译**

```bash
ZEPHYR_BASE=~/ncs/v3.4.0/zephyr nrfutil sdk-manager toolchain launch --ncs-version v3.4.0 -- \
  west build -b nrf52840dongle ble-receiver/firmware -d build-fw --no-sysbuild
```
Expected: 编译成功

- [ ] **Step 3: Commit**

```bash
git add ble-receiver/firmware/src/main.c
git commit -m "feat: simulated controller data with text output"
```

---

### Task 5: Makefile 更新 + 硬件验证

**Files:**
- Modify: `Makefile`

- [ ] **Step 1: 更新 Makefile 添加固件和测试 targets**

在 Makefile 的 `clean-cdc` target 之后，`devices` target 之前添加：

```makefile
# ── Firmware (BLE Receiver) ───────────────────────────────────
FW_DIR := ble-receiver/firmware
FW_BUILD := build-fw
TEST_DIR := ble-receiver/tests/unit/test_formatter_text
TEST_BUILD := build-test

build-fw:
	ZEPHYR_BASE=$(ZEPHYR_BASE) $(LAUNCH) west build -b $(BOARD) $(FW_DIR) -d $(FW_BUILD) --no-sysbuild

flash-fw: build-fw
	nrfutil nrf5sdk-tools pkg generate \
		--application $(FW_BUILD)/zephyr/zephyr.hex $(DFU_PKG_OPTS) \
		$(FW_BUILD)/zephyr/zephyr_dfu.zip
	nrfutil device program --firmware $(FW_BUILD)/zephyr/zephyr_dfu.zip $(DFU_TRAITS)

build-test:
	ZEPHYR_BASE=$(ZEPHYR_BASE) $(LAUNCH) west build -b native_sim $(TEST_DIR) -d $(TEST_BUILD) --no-sysbuild

run-test:
	ZEPHYR_BASE=$(ZEPHYR_BASE) $(LAUNCH) west build -t run -d $(TEST_BUILD)

clean-fw:
	rm -rf $(FW_BUILD)

clean-test:
	rm -rf $(TEST_BUILD)
```

同时更新 `.PHONY` 和 `clean` target：

```makefile
.PHONY: build-blinky build-cdc flash-blinky flash-cdc
.PHONY: build-fw flash-fw build-test run-test
.PHONY: clean clean-blinky clean-cdc clean-fw clean-test devices

clean: clean-blinky clean-cdc clean-fw clean-test
```

- [ ] **Step 2: 验证单元测试**

```bash
make build-test
make run-test
```
Expected: 10 个测试全部 PASS

- [ ] **Step 3: 构建固件**

```bash
make build-fw
```
Expected: 编译成功，`build-fw/zephyr/zephyr.elf` 生成

- [ ] **Step 4: 烧录到 Dongle (Dongle 进入 DFU 模式)**

```bash
make flash-fw
```
Expected: DFU zip 生成成功，烧录成功

- [ ] **Step 5: 硬件验证**

1. PC 上打开串口工具（如 `minicom -D /dev/ttyACM0` 或 `screen /dev/ttyACM0 115200`）
2. Dongle LED 指示 USB 已枚举
3. 串口收到文本行：`LX:04096,LY:-4096,RX:02048,RY:-02048,BTN:0000,LT:032,RT:223\r\n`
4. 每 500ms 更新一次，摇杆值循环递增
5. 验证数据格式正确：6位摇杆、4位按钮hex、3位扳机

- [ ] **Step 6: Commit**

```bash
git add Makefile
git commit -m "feat: Makefile targets for firmware build/test/flash"
```

- [ ] **Step 7: 更新 design.md 勾选 M1 任务**

在 `ble-receiver/docs/design.md` 的 M1 任务列表中勾选所有已完成项：

```
- [x] 实现 controller_state 数据结构 (src/controller_state.h)
- [x] 实现文本格式化器 (src/formatter_text.c)
- [x] 实现 USB CDC output backend (src/output_cdc.c)
- [x] 实现统一输出接口 output_send(buf, len) (src/output.h)
- [x] 用定时器生成模拟 controller_state 数据 (src/main.c)
- [x] 编写文本格式化单元测试 (tests/unit/test_formatter_text.c)
```

```bash
git add ble-receiver/docs/design.md
git commit -m "docs: mark M1 tasks complete"
```
