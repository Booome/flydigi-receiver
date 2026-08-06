# M2 设计：二进制格式化

## 一、概述

### 1.1 目标

实现二进制帧格式化器，验证 16 字节二进制帧格式。与 M1 的文本格式化器
并列，通过 Kconfig 编译时切换。

### 1.2 范围

- 实现 `formatter_binary.h/.c`（二进制帧格式化）
- 修改 `main.c`（添加 BINARY 条件编译分支）
- 修改 `Makefile`（支持 `TEST=` 参数切换测试目录）
- 编写单元测试（`tests/unit/test_formatter_binary/`）
- 编写 PC 端解析脚本（`tests/scripts/parse_binary.py`）

### 1.3 不在范围内

- Kconfig 修改（`OUTPUT_FORMAT_BINARY` 选项 M1 已创建）
- CMakeLists.txt 修改（已 GLOB `src/*.c`，自动包含）
- prj.conf 修改（验证时临时切换，不固化到默认配置）
- 硬件集成测试（M7 端到端时验证）

---

## 二、帧格式

来自 `design.md` 5.2 节，16 字节固定长度：

```
偏移  大小  字段        类型        说明
0     1    frame_head1  uint8      0xAA
1     1    frame_head2  uint8      0x55
2     1    length       uint8      有效数据长度（固定 13）
3-4   2    buttons      uint16 LE  按钮位掩码
5-6   2    lx           int16 LE   左摇杆 X
7-8   2    ly           int16 LE   左摇杆 Y
9-10  2    rx           int16 LE   右摇杆 X
11-12 2    ry           int16 LE   右摇杆 Y
13    1    lt           uint8      左扳机
14    1    rt           uint8      右扳机
15    1    checksum     uint8      bytes[2..14] 累加和 & 0xFF
```

### 2.1 设计说明

- **checksum 范围**：bytes[2..14]，共 13 字节（length + buttons×2 +
  lx×2 + ly×2 + rx×2 + ry×2 + lt + rt）
- **battery 字段**：`controller_state.battery` 不包含在帧中
  （design.md 帧格式未定义）
- **字节序**：使用 Zephyr `sys_put_le16()` 写入小端序
  （来自 `<zephyr/sys/byteorder.h>`）

---

## 三、接口设计

### 3.1 formatter_binary.h

```c
#ifndef FORMATTER_BINARY_H
#define FORMATTER_BINARY_H

#include "controller_state.h"
#include <stddef.h>
#include <stdint.h>

#define FORMATTER_BINARY_FRAME_LEN 16

/*
 * Format controller_state into a binary frame.
 * Frame: [0xAA][0x55][len][buttons LE][lx LE][ly LE][rx LE][ry LE][lt][rt][checksum]
 * Returns FORMATTER_BINARY_FRAME_LEN (16) on success.
 * Returns 0 if buf_size < FORMATTER_BINARY_FRAME_LEN.
 */
size_t formatter_binary_format(const struct controller_state *state,
                               uint8_t *buf, size_t buf_size);

#endif /* FORMATTER_BINARY_H */
```

### 3.2 formatter_binary.c 实现要点

1. 检查 `buf_size < FORMATTER_BINARY_FRAME_LEN`，不足返回 0
2. 写入帧头 `buf[0] = 0xAA, buf[1] = 0x55`
3. 写入长度 `buf[2] = 13`
4. 用 `sys_put_le16()` 写入 buttons, lx, ly, rx, ry 到 buf[3..12]
5. 写入 `buf[13] = lt, buf[14] = rt`
6. 计算 checksum：累加 buf[2..14] 并 `& 0xFF`，写入 buf[15]
7. 返回 16

---

## 四、main.c 修改

在 `#if IS_ENABLED(CONFIG_OUTPUT_FORMAT_TEXT)` 分支后添加
`#elif IS_ENABLED(CONFIG_OUTPUT_FORMAT_BINARY)` 分支：

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

同时在文件头添加 `#include "formatter_binary.h"`。

---

## 五、Makefile 修改

当前 Makefile 的 `TEST_DIR` 硬编码为 `test_formatter_text`，需要改为
支持 `TEST=` 参数：

```makefile
TEST_NAME ?= test_formatter_text
TEST_DIR := ble-receiver/tests/unit/$(TEST_NAME)

build-test:
	ZEPHYR_BASE=$(ZEPHYR_BASE) $(LAUNCH) west build -b native_sim $(TEST_DIR) -d $(TEST_BUILD) --no-sysbuild

run-test:
	ZEPHYR_BASE=$(ZEPHYR_BASE) $(LAUNCH) west build -t run -d $(TEST_BUILD)
```

使用方式：
- `make build-test`（默认 test_formatter_text）
- `make build-test TEST=test_formatter_binary`
- `make run-test TEST=test_formatter_binary`

---

## 六、单元测试

### 6.1 测试目录结构

```
tests/unit/test_formatter_binary/
  CMakeLists.txt
  prj.conf
  testcase.yaml
  src/
    test_formatter_binary.c
```

复用 M1 的测试框架模式（CMakeLists.txt 引用 `../../../firmware/src/`，
prj.conf 只有 `CONFIG_ZTEST=y`，testcase.yaml 指定 `native_sim`）。

### 6.2 测试用例

| # | 测试函数 | 验证内容 |
|---|---------|---------|
| 1 | test_frame_header | buf[0]==0xAA, buf[1]==0x55 |
| 2 | test_length_field | buf[2]==13 |
| 3 | test_buttons_le | buttons 小端序写入 buf[3..4] |
| 4 | test_sticks_le | lx/ly/rx/ry 小端序写入 buf[5..12] |
| 5 | test_triggers | lt/rt 正确写入 buf[13..14] |
| 6 | test_checksum_zero | 全零状态 checksum 正确 |
| 7 | test_checksum_max | 极值状态（buttons=0xFFFF, sticks=±32768, triggers=255）checksum 正确 |
| 8 | test_checksum_random | 随机组合值 checksum 正确 |
| 9 | test_frame_length | 返回值 == 16 |
| 10 | test_buffer_overflow | buf_size < 16 返回 0 |

### 6.3 测试策略

- checksum 验证：在测试中独立计算预期 checksum，与 buf[15] 比较
- 小端序验证：用 `sys_get_le16()` 读回，或直接检查 buf 字节
- 极值测试：int16_t 的 -32768 和 32767，uint8_t 的 0 和 255

---

## 七、Python 解析脚本

### 7.1 文件

`tests/scripts/parse_binary.py`

### 7.2 功能

1. 打开串口（默认 `/dev/ttyACM0`，115200 baud）
2. 同步帧头：逐字节读取直到找到 `0xAA 0x55`
3. 读取剩余 14 字节，组成 16 字节完整帧
4. 验证 checksum（不一致则跳过并重新同步）
5. 用 `struct.unpack` 解析各字段
6. 打印解析结果（hex + 十进制）
7. 持续接收直到 Ctrl+C 或达到 `--count` 指定帧数

### 7.3 命令行参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--port` | `/dev/ttyACM0` | 串口设备路径 |
| `--count` | 0（无限） | 接收帧数，0 为持续接收 |

---

## 八、验证标准

### 8.1 单元测试（native_sim）

- 10/10 测试用例全部通过
- 命令：`make build-test TEST=test_formatter_binary && make run-test TEST=test_formatter_binary`

### 8.2 硬件验证

- 临时修改 prj.conf：`CONFIG_OUTPUT_FORMAT_TEXT=y` ->
  `CONFIG_OUTPUT_FORMAT_BINARY=y`
- 编译：`make build-fw`
- 烧录后串口收到 16 字节二进制帧（非文本）
- 验证后恢复 prj.conf 为 TEXT 模式

### 8.3 Python 脚本验证

- 接收串口二进制帧
- 帧头、长度、checksum 校验通过
- 各字段值与模拟数据变化一致
