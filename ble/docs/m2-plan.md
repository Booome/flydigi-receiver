# M2 Binary Formatter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a 16-byte binary frame formatter with unit tests and a PC-side Python parser.

**Architecture:** Parallel to M1's text formatter - new `formatter_binary.c` with same interface pattern. Kconfig already has `OUTPUT_FORMAT_BINARY`; `main.c` gets a new `#elif` branch. Makefile gains `TEST=` parameter.

**Tech Stack:** C (Zephyr RTOS), ztest (native_sim), Python 3 (pyserial, struct)

## Global Constraints

- Zephyr RTOS (NCS v3.4.0), board `nrf52840dongle`, `--no-sysbuild`
- `ZEPHYR_BASE=~/ncs/v3.4.0/zephyr` required (Makefile handles)
- Build via `make build-*`, never call `west` directly
- NCS env: `nrfutil sdk-manager toolchain launch --ncs-version v3.4.0 --`
- Code: English only, no comments unless asked
- Docs: Chinese only (under `docs/`)
- Tests run on `native_sim` platform
- Worktree: `.worktrees/m2` on branch `feature/m2-implementation`

---

## File Structure

| Action | File | Responsibility |
|--------|------|----------------|
| Create | `ble-receiver/firmware/src/formatter_binary.h` | Interface + `FORMATTER_BINARY_FRAME_LEN` |
| Create | `ble-receiver/firmware/src/formatter_binary.c` | Frame implementation |
| Modify | `ble-receiver/firmware/src/main.c` | Add `#elif CONFIG_OUTPUT_FORMAT_BINARY` branch |
| Modify | `Makefile` | `TEST_NAME ?=` parameter |
| Create | `ble-receiver/tests/unit/test_formatter_binary/CMakeLists.txt` | Test build config |
| Create | `ble-receiver/tests/unit/test_formatter_binary/prj.conf` | `CONFIG_ZTEST=y` |
| Create | `ble-receiver/tests/unit/test_formatter_binary/testcase.yaml` | native_sim config |
| Create | `ble-receiver/tests/unit/test_formatter_binary/src/test_formatter_binary.c` | 10 ztest cases |
| Create | `ble-receiver/tests/scripts/parse_binary.py` | PC-side serial parser |

---

## Task 1: Binary Formatter + Unit Tests

**Files:**
- Create: `ble-receiver/firmware/src/formatter_binary.h`
- Create: `ble-receiver/firmware/src/formatter_binary.c`
- Create: `ble-receiver/tests/unit/test_formatter_binary/CMakeLists.txt`
- Create: `ble-receiver/tests/unit/test_formatter_binary/prj.conf`
- Create: `ble-receiver/tests/unit/test_formatter_binary/testcase.yaml`
- Create: `ble-receiver/tests/unit/test_formatter_binary/src/test_formatter_binary.c`

**Interfaces:**
- Consumes: `struct controller_state` from `controller_state.h` (M1)
- Produces: `formatter_binary_format(state, buf, size) -> size_t`, `FORMATTER_BINARY_FRAME_LEN`

- [ ] **Step 1: Write `formatter_binary.h`**

```c
#ifndef FORMATTER_BINARY_H
#define FORMATTER_BINARY_H

#include "controller_state.h"
#include <stddef.h>
#include <stdint.h>

#define FORMATTER_BINARY_FRAME_LEN 16

size_t formatter_binary_format(const struct controller_state *state,
                               uint8_t *buf, size_t buf_size);

#endif /* FORMATTER_BINARY_H */
```

- [ ] **Step 2: Write test file `test_formatter_binary.c`**

```c
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>
#include "controller_state.h"
#include "formatter_binary.h"

ZTEST_SUITE(formatter_binary_tests, NULL, NULL, NULL, NULL, NULL);

static uint8_t calc_checksum(const uint8_t *buf)
{
    uint8_t sum = 0;
    for (int i = 2; i <= 14; i++) {
        sum += buf[i];
    }
    return sum;
}

ZTEST(formatter_binary_tests, test_frame_header)
{
    struct controller_state state = {0};
    uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
    formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal(buf[0], 0xAA, "expected 0xAA, got 0x%02x", buf[0]);
    zassert_equal(buf[1], 0x55, "expected 0x55, got 0x%02x", buf[1]);
}

ZTEST(formatter_binary_tests, test_length_field)
{
    struct controller_state state = {0};
    uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
    formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal(buf[2], 13, "expected 13, got %d", buf[2]);
}

ZTEST(formatter_binary_tests, test_buttons_le)
{
    struct controller_state state = { .buttons = 0xABCD };
    uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
    formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal(buf[3], 0xCD, "low byte");
    zassert_equal(buf[4], 0xAB, "high byte");
    zassert_equal(sys_get_le16(&buf[3]), 0xABCD, "LE readback");
}

ZTEST(formatter_binary_tests, test_sticks_le)
{
    struct controller_state state = {
        .lx = 12345, .ly = -5432, .rx = 32767, .ry = -32768,
    };
    uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
    formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal((int16_t)sys_get_le16(&buf[5]), 12345, "lx");
    zassert_equal((int16_t)sys_get_le16(&buf[7]), -5432, "ly");
    zassert_equal((int16_t)sys_get_le16(&buf[9]), 32767, "rx");
    zassert_equal((int16_t)sys_get_le16(&buf[11]), -32768, "ry");
}

ZTEST(formatter_binary_tests, test_triggers)
{
    struct controller_state state = { .lt = 200, .rt = 55 };
    uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
    formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal(buf[13], 200, "lt");
    zassert_equal(buf[14], 55, "rt");
}

ZTEST(formatter_binary_tests, test_checksum_zero)
{
    struct controller_state state = {0};
    uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
    formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal(buf[15], calc_checksum(buf), "checksum");
}

ZTEST(formatter_binary_tests, test_checksum_max)
{
    struct controller_state state = {
        .buttons = 0xFFFF, .lx = 32767, .ly = 32767,
        .rx = 32767, .ry = 32767, .lt = 255, .rt = 255,
    };
    uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
    formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal(buf[15], calc_checksum(buf), "checksum");
}

ZTEST(formatter_binary_tests, test_checksum_random)
{
    struct controller_state state = {
        .buttons = 0x0F0F, .lx = -12345, .ly = 24680,
        .rx = -1, .ry = 1, .lt = 128, .rt = 64,
    };
    uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
    formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal(buf[15], calc_checksum(buf), "checksum");
}

ZTEST(formatter_binary_tests, test_frame_length)
{
    struct controller_state state = {0};
    uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
    size_t len = formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal(len, FORMATTER_BINARY_FRAME_LEN, "len");
}

ZTEST(formatter_binary_tests, test_buffer_overflow)
{
    struct controller_state state = {0};
    uint8_t buf[10];
    size_t len = formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal(len, 0, "expected 0");
}
```

- [ ] **Step 3: Write test build files**

`CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_formatter_binary)

target_sources(app PRIVATE
    src/test_formatter_binary.c
    ../../../firmware/src/formatter_binary.c
)
target_include_directories(app PRIVATE ../../../firmware/src)
```

`prj.conf`:
```ini
CONFIG_ZTEST=y
```

`testcase.yaml`:
```yaml
tests:
  ble_receiver.test_formatter_binary:
    platform_allow:
      - native_sim
    integration_platforms:
      - native_sim
    tags:
      - formatter
      - unit
```

- [ ] **Step 4: Run tests to verify they fail**

Run: `make build-test TEST=test_formatter_binary`
Expected: BUILD FAIL - `formatter_binary_format` undefined

- [ ] **Step 5: Write `formatter_binary.c` implementation**

```c
#include "formatter_binary.h"
#include <zephyr/sys/byteorder.h>

#define FRAME_HEAD1 0xAA
#define FRAME_HEAD2 0x55
#define FRAME_PAYLOAD_LEN 13

size_t formatter_binary_format(const struct controller_state *state,
                               uint8_t *buf, size_t buf_size)
{
    if (buf_size < FORMATTER_BINARY_FRAME_LEN) {
        return 0;
    }

    buf[0] = FRAME_HEAD1;
    buf[1] = FRAME_HEAD2;
    buf[2] = FRAME_PAYLOAD_LEN;

    sys_put_le16(state->buttons, &buf[3]);
    sys_put_le16((uint16_t)state->lx, &buf[5]);
    sys_put_le16((uint16_t)state->ly, &buf[7]);
    sys_put_le16((uint16_t)state->rx, &buf[9]);
    sys_put_le16((uint16_t)state->ry, &buf[11]);

    buf[13] = state->lt;
    buf[14] = state->rt;

    uint8_t checksum = 0;
    for (int i = 2; i <= 14; i++) {
        checksum += buf[i];
    }
    buf[15] = checksum;

    return FORMATTER_BINARY_FRAME_LEN;
}
```

- [ ] **Step 6: Run tests to verify all pass**

Run: `make build-test TEST=test_formatter_binary && make run-test TEST=test_formatter_binary`
Expected: 10/10 tests PASS

- [ ] **Step 7: Commit**

```bash
git add ble-receiver/firmware/src/formatter_binary.h \
        ble-receiver/firmware/src/formatter_binary.c \
        ble-receiver/tests/unit/test_formatter_binary/
git commit -m "feat: add binary formatter with 10 unit tests"
```

---

## Task 2: main.c Integration + Makefile Update

**Files:**
- Modify: `ble-receiver/firmware/src/main.c`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `formatter_binary_format()` from Task 1
- Produces: firmware binary with `CONFIG_OUTPUT_FORMAT_BINARY=y`; Makefile supports `TEST=` parameter

- [ ] **Step 1: Modify `main.c` - add include**

Add after `#include "formatter_text.h"`:
```c
#include "formatter_binary.h"
```

- [ ] **Step 2: Modify `main.c` - add BINARY branch**

Change the `#if IS_ENABLED(CONFIG_OUTPUT_FORMAT_TEXT)` block to:
```c
#if IS_ENABLED(CONFIG_OUTPUT_FORMAT_TEXT)
        char buf[FORMATTER_TEXT_MAX_LEN];
        size_t len = formatter_text_format(&state, buf, sizeof(buf));
        output_send((const uint8_t *)buf, len);
#elif IS_ENABLED(CONFIG_OUTPUT_FORMAT_BINARY)
        uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
        size_t len = formatter_binary_format(&state, buf, sizeof(buf));
        output_send(buf, len);
#endif
```

- [ ] **Step 3: Modify `Makefile` - support `TEST=` parameter**

Change:
```makefile
TEST_DIR := ble-receiver/tests/unit/test_formatter_text
```
To:
```makefile
TEST_NAME ?= test_formatter_text
TEST_DIR := ble-receiver/tests/unit/$(TEST_NAME)
```

- [ ] **Step 4: Verify text test still passes (regression)**

Run: `make build-test && make run-test`
Expected: 10/10 tests PASS (test_formatter_text, default)

- [ ] **Step 5: Verify binary test passes**

Run: `make build-test TEST=test_formatter_binary && make run-test TEST=test_formatter_binary`
Expected: 10/10 tests PASS

- [ ] **Step 6: Verify firmware builds (TEXT mode)**

Run: `make build-fw`
Expected: BUILD SUCCESS

- [ ] **Step 7: Commit**

```bash
git add ble-receiver/firmware/src/main.c Makefile
git commit -m "feat: integrate binary formatter into main + Makefile TEST= support"
```

---

## Task 3: Python Binary Frame Parser

**Files:**
- Create: `ble-receiver/tests/scripts/parse_binary.py`

**Interfaces:**
- Consumes: serial output from firmware (16-byte binary frames)
- Produces: parsed console output with field validation

- [ ] **Step 1: Write `parse_binary.py`**

```python
#!/usr/bin/env python3
"""Parse binary frames from Flydigi BLE Receiver serial output."""

import argparse
import struct
import sys
import time

FRAME_LEN = 16
HEAD1 = 0xAA
HEAD2 = 0x55
PAYLOAD_LEN = 13

try:
    import serial
except ImportError:
    print("Error: pyserial not installed. Run: pip install pyserial", file=sys.stderr)
    sys.exit(1)


def calc_checksum(frame):
    return sum(frame[2:15]) & 0xFF


def sync_frame(ser):
    """Read bytes until we find 0xAA 0x55 header."""
    while True:
        b = ser.read(1)
        if len(b) == 0:
            return None
        if b[0] == HEAD1:
            b2 = ser.read(1)
            if len(b2) == 1 and b2[0] == HEAD2:
                return True
    return None


def parse_frame(frame):
    """Parse a 16-byte frame. Returns dict or None if invalid."""
    if len(frame) != FRAME_LEN:
        return None
    if frame[0] != HEAD1 or frame[1] != HEAD2:
        return None

    length = frame[2]
    buttons, lx, ly, rx, ry = struct.unpack_from('<HHHHH', frame, 3)
    lt = frame[13]
    rt = frame[14]
    checksum = frame[15]

    expected = calc_checksum(frame)
    valid = (checksum == expected) and (length == PAYLOAD_LEN)

    return {
        'length': length,
        'buttons': buttons,
        'lx': struct.unpack_from('<h', frame, 5)[0],
        'ly': struct.unpack_from('<h', frame, 7)[0],
        'rx': struct.unpack_from('<h', frame, 9)[0],
        'ry': struct.unpack_from('<h', frame, 11)[0],
        'lt': lt,
        'rt': rt,
        'checksum': checksum,
        'valid': valid,
    }


def main():
    parser = argparse.ArgumentParser(description='Parse binary frames from serial')
    parser.add_argument('--port', default='/dev/ttyACM0', help='Serial port')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate')
    parser.add_argument('--count', type=int, default=0, help='Number of frames (0=forever)')
    args = parser.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=2.0)
    print(f"Listening on {args.port} @ {args.baud} baud...", file=sys.stderr)

    count = 0
    try:
        while args.count == 0 or count < args.count:
            if not sync_frame(ser):
                print("Timeout waiting for frame header", file=sys.stderr)
                continue

            rest = ser.read(FRAME_LEN - 2)
            if len(rest) < FRAME_LEN - 2:
                print("Incomplete frame", file=sys.stderr)
                continue

            frame = bytes([HEAD1, HEAD2]) + rest
            data = parse_frame(frame)

            if data is None:
                print(f"Invalid frame: {frame.hex()}", file=sys.stderr)
                continue

            status = "OK" if data['valid'] else "BAD"
            print(f"[{count:4d}] {status} | "
                  f"BTN:{data['buttons']:04x} "
                  f"LX:{data['lx']:+6d} LY:{data['ly']:+6d} "
                  f"RX:{data['rx']:+6d} RY:{data['ry']:+6d} "
                  f"LT:{data['lt']:3d} RT:{data['rt']:3d} "
                  f"CHK:{data['checksum']:02x}")
            count += 1

    except KeyboardInterrupt:
        print(f"\nStopped. {count} frames received.", file=sys.stderr)
    finally:
        ser.close()


if __name__ == '__main__':
    main()
```

- [ ] **Step 2: Verify script syntax**

Run: `python3 -c "import py_compile; py_compile.compile('ble-receiver/tests/scripts/parse_binary.py', doraise=True)"`
Expected: No output (success)

- [ ] **Step 3: Commit**

```bash
git add ble-receiver/tests/scripts/parse_binary.py
git commit -m "feat: add Python binary frame parser script"
```

---

## Self-Review

**Spec coverage:** All items from m2-design.md covered:
- formatter_binary.h/.c -> Task 1
- main.c modification -> Task 2
- Makefile TEST= support -> Task 2
- 10 unit tests -> Task 1
- parse_binary.py -> Task 3

**Placeholder scan:** No TBD/TODO. All code blocks contain complete implementations.

**Type consistency:** `formatter_binary_format(state, buf, size) -> size_t` consistent across header, implementation, tests, and main.c. `FORMATTER_BINARY_FRAME_LEN` used consistently.
