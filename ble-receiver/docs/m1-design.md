# M1 实现设计：USB CDC 后端 + 文本格式化 + 模拟数据

## 概述

M1 目标是验证输出层和文本格式化，不依赖手柄。通过定时器生成模拟
`controller_state` 数据，经文本格式化器转换为串口文本行，通过 USB CDC
后端输出到 PC。

本文档是 `design.md` 中 M1 里程碑的详细实现设计，聚焦实现层面的决策。
数据结构、格式定义等已在 `design.md` 中确定，本文档不重复。

## 一、项目结构

采用"测试/固件分离"模式（NCS 标准惯例）：

```
ble-receiver/
  firmware/
    CMakeLists.txt             # 固件构建 (nrf52840dongle)
    prj.conf                   # 固件配置
    Kconfig                    # 自定义 Kconfig 配置项
    src/
      main.c                   # 入口 + 模拟数据主循环
      controller_state.h       # 数据结构 + 按钮宏（design.md 已定义）
      formatter_text.h         # 文本格式化器接口
      formatter_text.c         # 文本格式化器实现
      output.h                 # 统一输出接口
      output_cdc.c             # USB CDC 后端实现
  tests/
    unit/
      test_formatter_text/
        CMakeLists.txt         # 测试构建 (native_sim)
        prj.conf               # CONFIG_ZTEST=y
        testcase.yaml
        src/
          test_formatter_text.c
```

- 测试通过 CMake 相对路径引用 `firmware/src/formatter_text.c`（NCS 成熟模式）
- `west build -b native_sim` 构建，`west build -t run` 运行
- 后续 M2+ 可复用同一测试框架

## 二、格式化器接口

```c
// formatter_text.h
#include "controller_state.h"
#include <stddef.h>

#define FORMATTER_TEXT_MAX_LEN 64  /* 实际 60 字节 + null */

/*
 * Format controller_state into text line.
 * Returns bytes written (excluding null terminator).
 * If buf_size is too small, returns 0 and does not write.
 */
size_t formatter_text_format(const struct controller_state *state,
                             char *buf, size_t buf_size);
```

- 格式串严格按 design.md：
  `LX:%06d,LY:%06d,RX:%06d,RY:%06d,BTN:%04x,LT:%03d,RT:%03d\r\n`
- 实现用 `snprintf`，自动处理缓冲区边界
- `battery` 字段不包含在文本输出中
- 最大输出长度 60 字节（4 字段 * (3+6) + 1 字段 * (4+4) + 2 字段 * (3+3) + 6 逗号 + 2 CRLF = 60）

## 三、输出接口与 USB CDC 后端

### 统一接口

```c
// output.h
#ifndef OUTPUT_H
#define OUTPUT_H

#include <stddef.h>

/* Initialize the output backend. Returns 0 on success. */
int output_init(void);

/* Send data through the output backend.
 * Returns 0 on success (including when CDC not ready - data discarded).
 * Returns negative errno on error.
 */
int output_send(const uint8_t *data, size_t len);

#endif
```

### USB CDC 实现 (output_cdc.c)

- `output_init()`：初始化 USB 设备栈 + CDC ACM 接口，注册枚举完成回调
- `output_send()`：检查 CDC 就绪标志
  - 就绪：通过 CDC ACM 写入数据
  - 未就绪：返回 0，丢弃数据（不阻塞模拟数据主循环）
- 参考已有 CDC ACM sample（M0 验证过的 `~/ncs/v3.4.0/zephyr/samples/subsys/usb/cdc_acm`）
- 通过 `#if IS_ENABLED(CONFIG_OUTPUT_CDC)` 条件编译
- 后续 M3+ 添加 `output_uart.c` / `output_hid.c` 实现同一接口

## 四、模拟数据策略

`main.c` 用主循环 + `k_sleep(500ms)`，不需要定时器/工作队列：

```c
void main(void) {
    output_init();
    struct controller_state state = {0};
    while (1) {
        sim_update(&state);
        char buf[FORMATTER_TEXT_MAX_LEN];
        size_t len = formatter_text_format(&state, buf, sizeof(buf));
        output_send((const uint8_t *)buf, len);
        k_sleep(K_MSEC(500));
    }
}
```

`sim_update()` 循环递增逻辑：

| 字段 | 步长 | 周期 | 说明 |
|------|------|------|------|
| lx | +4096 | 16 步 (8s) | 走完 int16_t 全范围 |
| ly | -lx | 同步 | 反向，验证负数格式 |
| rx/ry | ±lx/2 | 同步 | 半幅，验证不同值 |
| lt | +32 | 8 步 (4s) | 走完 0-255 |
| rt | 255-lt | 同步 | 反向 |
| buttons | 每 4 tick 翻转 1 位 | 60 步 (30s) | bit 0->14 逐位轮转 |

- 每 500ms 一次，摇杆周期 8 秒，扳机周期 4 秒，按钮周期 30 秒
- 简单可预测，能覆盖所有字段和极值

## 五、Kconfig 体系

M1 提前建立完整 Kconfig 结构，后续里程碑直接添加选项：

```kconfig
# firmware/Kconfig

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
    select USB_DEVICE_STACK
    select USB_CDC_ACM

config OUTPUT_UART
    bool "UART TTL"
    select SERIAL

config OUTPUT_HID
    bool "USB HID (transparent passthrough)"
    select USB_DEVICE_STACK
endchoice
```

- `select` 自动拉入所需 Zephyr 子系统
- `output_cdc.c` 用 `#if IS_ENABLED(CONFIG_OUTPUT_CDC)` 条件编译
- `main.c` 中通过 `#if IS_ENABLED(CONFIG_OUTPUT_FORMAT_TEXT)` 选择调用哪个格式化器
- `formatter_text.c` 本身不做条件编译，测试可直接引用

M1 的 `prj.conf`：

```
CONFIG_OUTPUT_FORMAT_TEXT=y
CONFIG_OUTPUT_CDC=y
CONFIG_USB_DEVICE_STACK=y
CONFIG_USB_CDC_ACM=y
CONFIG_SERIAL=y
```

## 六、构建系统

### 固件 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ble_receiver)
FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
target_include_directories(app PRIVATE src)
```

### 测试 CMakeLists.txt

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

### 测试 prj.conf

```
CONFIG_ZTEST=y
```

### 测试 testcase.yaml

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

### Makefile 新增 targets

```makefile
FW_DIR      := ble-receiver/firmware
FW_BUILD    := build-fw
TEST_DIR    := ble-receiver/tests/unit/test_formatter_text
TEST_BUILD  := build-test

build-fw:
    # west build -b nrf52840dongle ble-receiver/firmware

flash-fw: build-fw
    # DFU zip 生成 + 烧录（同 flash-blinky 流程）

build-test:
    # west build -b native_sim tests/.../test_formatter_text

run-test:
    # west build -t run -d build-test

clean-fw / clean-test
```

- native_sim 构建同样需要 `ZEPHYR_BASE`（测试目录也不在 NCS workspace 内）
- `make run-test` 直接在 PC 上运行 ztest，输出 PASS/FAIL

## 七、单元测试

`test_formatter_text.c` 用 ZTEST 宏，测试用例直接对应 design.md 的测试标准：

| 测试用例 | 输入 | 预期输出关键部分 |
|---------|------|----------------|
| `test_zero_state` | 全零 | `LX:000000,...,BTN:0000,LT:000,RT:000\r\n` |
| `test_stick_max` | 32767 | `LX:032767,...` |
| `test_stick_min` | -32768 | `LX:-32768,...` |
| `test_trigger_max` | 255 | `...,LT:255,RT:255\r\n` |
| `test_trigger_mid` | 128 | `...,LT:128,RT:128\r\n` |
| `test_buttons_all` | 0xFFFF | `...,BTN:ffff,...` |
| `test_buttons_single` | 循环 bit 0-14 | 逐个验证 `%04x` 位掩码正确 |
| `test_format_stable` | 同一输入调用 10 次 | 每次结果完全一致 |
| `test_crlf` | 任意输入 | 最后 2 字节为 `\r\n` |
| `test_buffer_overflow` | buf_size=10 | 返回 0，不越界写入 |

- 测试运行在 PC 上（native_sim 编译为 Linux 可执行文件）
- 不需要 Dongle 硬件
- `make build-test && make run-test` 一键构建运行

## 八、验证标准

### 硬件验证（nrf52840dongle）

1. `make build-fw && make flash-fw` 烧录固件
2. PC 串口工具连接 Dongle CDC 串口
3. 收到文本行 `LX:00000,LY:00000,...\r\n`，模拟数据定时变化
4. 摇杆值循环递增，扳机值循环变化，按钮逐个翻转

### 单元测试验证（native_sim）

1. `make build-test && make run-test`
2. 10 个测试用例全部 PASS
3. 无内存越界或崩溃
