# BS21 星闪接收器开发计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 基于 BS21 开发板实现飞智八爪鱼5 的星闪 SLE 无线接收器，最终输出为 USB HID Xbox 手柄。

**Architecture:** 验证层（P0 环境搭建 + P1 手柄连通性验证）先确认可行性，构建层（P2 协议解析 + P3 CDC 输出 + P4 USB HID 输出）逐步构建完整功能。P1 是关键决策点，连接失败则项目终止。

**Tech Stack:** 海思 fbb_bs2x SDK (Hi2821/BS21E), C, RISC-V GCC, Python 3 (构建/解析脚本), ws63flash (烧录)

## Global Constraints

- SDK 为海思官方 **fbb_bs2x**（GitCode），开发模式为在 SDK 源码树内改代码编译
- 编译命令 `python3 build.py standard-bs21e-1100e`（在 `fbb_bs2x/src` 目录下）
- 烧录命令 `ws63flash --flash /dev/ttyUSB0 xxx.fwpkg -b460800`（官方 BurnTool 仅 Windows）
- 主路径用实际手柄开发，不拆卸原装 dongle
- 使用项目已有的 `controller_state.h` 数据结构（定义于 `wireless/bs21/src/`），不修改
- P1 失败则项目终止，不进入 P2-P4
- 所有串口输出通过 USB2 (CH340) 调试串口
- 所有代码注释使用英文
- Task 5 和 Task 16 需要查阅 fbb_bs2x SDK 头文件获取确切 API 名称，计划中提供了期望的接口签名和实现模式

> **选型变更说明（2026-08）**：本计划最初基于 XFusion 编写，现改为海思官方 fbb_bs2x。
> 原 Task 1-4 创建独立项目骨架（`wireless/bs21/CMakeLists.txt` + `prj.conf`）的假设已不适用，
> 接收器代码作为 fbb_bs2x SDK 内的 application target。Task 1-7 已按 fbb_bs2x 模式重写。

---

### Task 1: 创建 BS21 项目骨架

**Files:**
- Create: `wireless/bs21/CMakeLists.txt`
- Create: `wireless/bs21/prj.conf`
- Create: `wireless/bs21/src/main.c`

**Interfaces:**
- Produces: `main.c` 入口，空主循环

- [ ] **Step 1: 创建目录结构**

```bash
mkdir -p wireless/bs21/src wireless/bs21/tests wireless/bs21/scripts
```

- [ ] **Step 2: 创建 CMakeLists.txt**

```cmake
# wireless/bs21/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

set(APP_NAME bs21_receiver)
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

file(GLOB_RECURSE APP_SOURCES ${SRC_DIR}/*.c)

project(${APP_NAME})
add_executable(${APP_NAME} ${APP_SOURCES})
```

- [ ] **Step 3: 创建 prj.conf**

```
# wireless/bs21/prj.conf
CONFIG_DEBUG=y
CONFIG_UART=y
CONFIG_UART0=y
```

- [ ] **Step 4: 创建最小 main.c**

```c
// wireless/bs21/src/main.c
#include <stdio.h>

int main(void)
{
    printf("BS21 Receiver starting...\n");
    return 0;
}
```

- [ ] **Step 5: 编译验证**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e
```

Expected: 编译成功，无错误。

- [ ] **Step 6: 烧录验证**

```bash
ws63flash --flash /dev/ttyUSB0 standard-bs21e-1100e_all_in_one.fwpkg -b460800
```

Expected: 烧录成功，USB2 串口输出 "BS21 Receiver starting..."

- [ ] **Step 7: 提交**

```bash
git add wireless/bs21/
git commit -m "feat: add BS21 project skeleton"
```

---

### Task 2: 定义 controller_state.h

**Files:**
- Create: `wireless/bs21/src/controller_state.h`

**Interfaces:**
- Produces: `controller_state` 结构体（平台无关，无 Zephyr 依赖）

- [ ] **Step 1: 创建 controller_state.h**

```c
// wireless/bs21/src/controller_state.h
#ifndef CONTROLLER_STATE_H
#define CONTROLLER_STATE_H

#include <stdint.h>

#define BIT(n) (1UL << (n))

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

- [ ] **Step 2: 在 main.c 中添加 include 验证**

```c
#include "controller_state.h"
```

- [ ] **Step 3: 编译验证**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e
```

Expected: 编译成功，controller_state.h 无依赖问题。

- [ ] **Step 4: 提交**

```bash
git add wireless/bs21/src/controller_state.h wireless/bs21/src/main.c
git commit -m "feat: add controller_state data structure"
```

---

### Task 3: P0.3 - 双板 SLE 互验（板A T 节点广播）

**前提**: P0.1 fbb_bs2x 环境已完成，P0.2 Hello World 已验证。

**Files:**
- Modify: `wireless/bs21/prj.conf`
- Create: `wireless/bs21/src/sle_manager.h`
- Create: `wireless/bs21/src/sle_manager.c`
- Modify: `wireless/bs21/src/main.c`

**Interfaces:**
- Produces: `sle_init()`, `sle_start_announce()`, `sle_start_seek()`, 回调函数声明

- [ ] **Step 1: 创建 sle_manager.h 接口**

```c
// wireless/bs21/src/sle_manager.h
#ifndef SLE_MANAGER_H
#define SLE_MANAGER_H

#include <stdint.h>

typedef struct {
    uint8_t addr[6];
    int8_t rssi;
    uint8_t data_len;
    uint8_t data[31];
} sle_scan_result_t;

typedef void (*scan_result_cb_t)(const sle_scan_result_t *result);
typedef void (*connect_state_cb_t)(uint8_t conn_id, uint8_t state, uint8_t reason);
typedef void (*pair_complete_cb_t)(uint8_t conn_id, uint8_t status);
typedef void (*ssap_data_cb_t)(uint8_t conn_id, const uint8_t *data, uint16_t len);

void sle_init(void);
void sle_start_announce(void);
void sle_stop_announce(void);
void sle_start_seek(void);
void sle_stop_seek(void);
void sle_connect(const uint8_t addr[6]);
void sle_disconnect(uint8_t conn_id);
void sle_pair(uint8_t conn_id);
void sle_send(uint8_t conn_id, const uint8_t *data, uint16_t len);
void sle_set_scan_callback(scan_result_cb_t cb);
void sle_set_connect_callback(connect_state_cb_t cb);
void sle_set_pair_callback(pair_complete_cb_t cb);
void sle_set_data_callback(ssap_data_cb_t cb);

#endif
```

- [ ] **Step 2: 创建 sle_manager.c 骨架实现**

```c
// wireless/bs21/src/sle_manager.c
#include "sle_manager.h"
#include <stdio.h>
#include <string.h>

static scan_result_cb_t g_scan_cb = NULL;
static connect_state_cb_t g_connect_cb = NULL;
static pair_complete_cb_t g_pair_cb = NULL;
static ssap_data_cb_t g_data_cb = NULL;

void sle_init(void)
{
    printf("[SLE] init\n");
}

void sle_start_announce(void)
{
    printf("[SLE] announce started\n");
}

void sle_stop_announce(void)
{
    printf("[SLE] announce stopped\n");
}

void sle_start_seek(void)
{
    printf("[SLE] seek started\n");
}

void sle_stop_seek(void)
{
    printf("[SLE] seek stopped\n");
}

void sle_connect(const uint8_t addr[6])
{
    printf("[SLE] connect to %02X:%02X:%02X:%02X:%02X:%02X\n",
           addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

void sle_disconnect(uint8_t conn_id)
{
    printf("[SLE] disconnect conn_id=%u\n", conn_id);
}

void sle_pair(uint8_t conn_id)
{
    printf("[SLE] pair conn_id=%u\n", conn_id);
}

void sle_send(uint8_t conn_id, const uint8_t *data, uint16_t len)
{
    printf("[SLE] send conn_id=%u len=%u\n", conn_id, len);
}

void sle_set_scan_callback(scan_result_cb_t cb) { g_scan_cb = cb; }
void sle_set_connect_callback(connect_state_cb_t cb) { g_connect_cb = cb; }
void sle_set_pair_callback(pair_complete_cb_t cb) { g_pair_cb = cb; }
void sle_set_data_callback(ssap_data_cb_t cb) { g_data_cb = cb; }
```

- [ ] **Step 3: 更新 prj.conf 启用 SLE**

在 `wireless/bs21/prj.conf` 追加：

```
CONFIG_SUPPORT_SLE=y
CONFIG_SUPPORT_SLE_CENTRAL=y
CONFIG_SUPPORT_SLE_PERIPHERAL=y
```

- [ ] **Step 4: 修改 main.c 为 T 节点（板A）**

```c
// wireless/bs21/src/main.c  (板A: T 节点)
#include <stdio.h>
#include "sle_manager.h"

int main(void)
{
    printf("BS21 T-Node (Announce)\n");
    sle_init();
    sle_start_announce();
    while (1) {
        // SLE stack event loop
    }
    return 0;
}
```

- [ ] **Step 5: 编译并烧录板A**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e && ws63flash --flash /dev/ttyUSB0 standard-bs21e-1100e_all_in_one.fwpkg -b460800
```

Expected: 串口输出 "BS21 T-Node (Announce)" 和 "[SLE] announce started"。

- [ ] **Step 6: 提交**

```bash
git add wireless/bs21/
git commit -m "feat: add SLE manager skeleton with T-node announce"
```

---

### Task 4: P0.3 - 双板 SLE 互验（板B G 节点扫描）

**前提**: Task 3 完成，板A 正在广播。

**Files:**
- Modify: `wireless/bs21/src/main.c`

- [ ] **Step 1: 修改 main.c 为 G 节点（板B）**

```c
// wireless/bs21/src/main.c  (板B: G 节点)
#include <stdio.h>
#include "sle_manager.h"

static void on_scan_result(const sle_scan_result_t *result)
{
    printf("[SCAN] device: ");
    for (int i = 0; i < 6; i++) {
        printf("%02X", result->addr[i]);
        if (i < 5) printf(":");
    }
    printf(" RSSI=%d data_len=%d\n", result->rssi, result->data_len);
    if (result->data_len > 0) {
        printf("[SCAN]   data: ");
        for (int i = 0; i < result->data_len; i++) {
            printf("%02X ", result->data[i]);
        }
        printf("\n");
    }
}

int main(void)
{
    printf("BS21 G-Node (Seek)\n");
    sle_init();
    sle_set_scan_callback(on_scan_result);
    sle_start_seek();
    while (1) {
        // SLE stack event loop
    }
    return 0;
}
```

- [ ] **Step 2: 编译并烧录板B**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e && ws63flash --flash /dev/ttyUSB0 standard-bs21e-1100e_all_in_one.fwpkg -b460800
```

- [ ] **Step 3: 串口观察扫描结果**

用串口工具连接板B USB2 (CH340)，观察 `[SCAN] device:` 输出。

Expected: 看到板A 的 SLE 地址和广播数据（当前为骨架实现，仅打印日志）。

- [ ] **Step 4: 提交**

```bash
git add wireless/bs21/src/main.c
git commit -m "feat: add G-node seek main with scan callback"
```

---

### Task 5: P0.3 - 接入真实 SLE SDK API

**前提**: Task 3-4 骨架验证通过，确认项目结构正确。

**Files:**
- Modify: `wireless/bs21/src/sle_manager.c`
- Modify: `wireless/bs21/src/main.c`

**说明**: 将骨架替换为真实 SDK API 调用。由于 BS21 SDK 的 SLE API 名称和参数需要查阅实际头文件，此任务需要根据实际 SDK 调整。

- [ ] **Step 1: 查阅 SDK SLE 头文件**

```bash
find ~/fbb_bs2x/ -path "*/sle/*.h" | head -20
cat ~/fbb_bs2x/.../sle_device_manager.h
cat ~/fbb_bs2x/.../sle_ssap.h
```

- [ ] **Step 2: 替换 sle_init() 为真实实现**

在 `sle_manager.c` 中，将 `sle_init()` 替换为：
- 调用 `enable_sle()`
- 注册设备管理回调 `sle_dev_manager_register_callbacks()`

- [ ] **Step 3: 替换 sle_start_announce() 为真实实现**

在 `sle_manager.c` 中，将 `sle_start_announce()` 替换为：
- 调用 `sle_set_announce_data()` 设置广播数据
- 调用 `sle_start_announce()` 开始广播

- [ ] **Step 4: 替换 sle_start_seek() 为真实实现**

在 `sle_manager.c` 中，将 `sle_start_seek()` 替换为：
- 调用 `sle_set_seek_param()` 设置扫描参数
- 调用 `sle_start_seek()` 开始扫描
- 在 `seek_result_cb` 中调用 `g_scan_cb`

- [ ] **Step 5: 替换 sle_connect() 为真实实现**

在 `sle_manager.c` 中，将 `sle_connect()` 替换为：
- 调用 `sle_connect_remote_device(&addr)`

- [ ] **Step 6: 板A 编译烧录，验证广播**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e && ws63flash --flash /dev/ttyUSB0 standard-bs21e-1100e_all_in_one.fwpkg -b460800
```

Expected: 板A 串口输出确认 SLE 广播已启动。

- [ ] **Step 7: 板B 编译烧录，验证扫描**

修改 main.c 为 G 节点版本，编译烧录：

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e && ws63flash --flash /dev/ttyUSB0 standard-bs21e-1100e_all_in_one.fwpkg -b460800
```

Expected: 板B 扫描到板A，输出板A 的 SLE 地址和广播数据。

- [ ] **Step 8: 提交**

```bash
git add wireless/bs21/src/sle_manager.c wireless/bs21/src/main.c
git commit -m "feat: integrate real SLE SDK API calls"
```

---

### Task 6: P0.3 - 双板 SLE 连接

**前提**: Task 5 完成，板B 能扫描到板A。

**Files:**
- Modify: `wireless/bs21/src/main.c`
- Modify: `wireless/bs21/src/sle_manager.c`

- [ ] **Step 1: 在板B 扫描回调中自动发起连接**

```c
static void on_scan_result(const sle_scan_result_t *result)
{
    printf("[SCAN] device: %02X:%02X:%02X:%02X:%02X:%02X RSSI=%d\n",
           result->addr[0], result->addr[1], result->addr[2],
           result->addr[3], result->addr[4], result->addr[5],
           result->rssi);
    sle_connect(result->addr);
}
```

- [ ] **Step 2: 添加连接状态回调**

```c
static void on_connect_state(uint8_t conn_id, uint8_t state, uint8_t reason)
{
    printf("[CONN] conn_id=%u state=%u reason=%u\n", conn_id, state, reason);
}
```

- [ ] **Step 3: 在 main 中注册连接回调**

```c
sle_set_connect_callback(on_connect_state);
```

- [ ] **Step 4: 编译烧录板B，验证连接**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e && ws63flash --flash /dev/ttyUSB0 standard-bs21e-1100e_all_in_one.fwpkg -b460800
```

Expected: 板B 串口输出 `[CONN]` 回调，连接状态为已连接。

- [ ] **Step 5: 提交**

```bash
git add wireless/bs21/src/
git commit -m "feat: add SLE auto-connect on scan result"
```

---

### Task 7: P0.3 - 双板 SLE 配对 + SSAP 收发

**前提**: Task 6 完成，板B 已连接板A。

**Files:**
- Modify: `wireless/bs21/src/main.c`
- Modify: `wireless/bs21/src/sle_manager.c`

- [ ] **Step 1: 连接成功后自动发起配对**

```c
static void on_connect_state(uint8_t conn_id, uint8_t state, uint8_t reason)
{
    printf("[CONN] conn_id=%u state=%u reason=%u\n", conn_id, state, reason);
    if (state == 1) {  // connected
        sle_pair(conn_id);
    }
}
```

- [ ] **Step 2: 添加配对完成回调**

```c
static void on_pair_complete(uint8_t conn_id, uint8_t status)
{
    printf("[PAIR] conn_id=%u status=%u\n", conn_id, status);
    if (status == 0) {
        const char *msg = "ping";
        sle_send(conn_id, (const uint8_t *)msg, 4);
    }
}
```

- [ ] **Step 3: 添加数据接收回调**

```c
static void on_data_received(uint8_t conn_id, const uint8_t *data, uint16_t len)
{
    printf("[DATA] conn_id=%u len=%u: ", conn_id, len);
    for (int i = 0; i < len; i++) printf("%02X ", data[i]);
    printf("\n");
    // echo back: "pong"
    const char *resp = "pong";
    sle_send(conn_id, (const uint8_t *)resp, 4);
}
```

- [ ] **Step 4: 在 main 中注册所有回调**

```c
sle_set_connect_callback(on_connect_state);
sle_set_pair_callback(on_pair_complete);
sle_set_data_callback(on_data_received);
```

- [ ] **Step 5: 板A 也添加数据接收回调，接收 "ping" 后回复 "pong"**

板A main.c 添加：
```c
static void on_data_received(uint8_t conn_id, const uint8_t *data, uint16_t len)
{
    printf("[DATA] received: %.*s\n", len, data);
    const char *resp = "pong";
    sle_send(conn_id, (const uint8_t *)resp, 4);
}
```

- [ ] **Step 6: 编译烧录两块板，验证 ping/pong**

```bash
# 板A 先烧录，开始广播
# 板B 后烧录，开始扫描
```

Expected: 板B 串口输出显示连接→配对→发送 "ping"→收到 "pong"。

- [ ] **Step 7: 提交**

```bash
git add wireless/bs21/src/
git commit -m "feat: add SLE pairing and SSAP ping/pong"
```

---

## P0 完成检查点

**验证清单**:
- [ ] 板A 能 SLE 广播
- [ ] 板B 能扫描到板A
- [ ] 板B 能连接板A
- [ ] 板B 能配对角A
- [ ] 板A 和板B 能通过 SSAP 收发数据

**P0 通过后继续 P1，否则排查环境问题。**

---

### Task 8: P1.1 - 扫描手柄 SLE 广播

**前提**: P0 通过，双板 SLE 互验正常。

**Files:**
- Modify: `wireless/bs21/src/main.c`

- [ ] **Step 1: 手柄拨到 PC 模式（左侧），开机**

确认手柄白色 LED 亮起。

- [ ] **Step 2: 修改扫描参数，延长扫描时长**

在 `sle_manager.c` 的 `sle_start_seek()` 中，设置扫描参数为全信道、长扫描：

```c
// 在 sle_start_seek 中设置:
// seek_param.own_addr_type = PUBLIC_OR_RANDOM
// seek_param.filter_duplicates = false
// seek_param.seek_phy = 1M_PHY | 2M_PHY | 4M_PHY
// seek_param.seek_type = ACTIVE_SEEK
// seek_param.seek_timeout = 0xFFFF  // maximum
```

- [ ] **Step 3: 编译烧录板B，执行扫描**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e && ws63flash --flash /dev/ttyUSB0 standard-bs21e-1100e_all_in_one.fwpkg -b460800
```

- [ ] **Step 4: 观察串口输出，筛选手柄设备**

遍历所有 `[SCAN] device:` 输出，寻找：
- 广播数据中包含 "Flydigi" 或 "K5" 字符串
- 广播数据中包含已知的厂商 ID
- RSSI 较强的设备（手柄靠近开发板）

- [ ] **Step 5: 记录手柄 SLE 信息**

记录以下内容到文档：
- 手柄 SLE 地址（6 字节）
- 广播模式（判断是 CONNECTABLE 还是 DIRECTED）
- RSSI 值
- 广播数据 hex dump（完整 31 字节）

- [ ] **Step 6: 提交**

```bash
git add wireless/bs21/src/
git commit -m "feat: scan for controller SLE broadcast"
```

---

### Task 9: P1.2 - 连接手柄

**前提**: Task 8 完成，获取了手柄 SLE 地址。

**Files:**
- Modify: `wireless/bs21/src/main.c`

- [ ] **Step 1: 硬编码手柄地址，扫描到后自动连接**

```c
// 用 Task 8 获取的手柄地址替换占位符
static const uint8_t CONTROLLER_ADDR[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static void on_scan_result(const sle_scan_result_t *result)
{
    printf("[SCAN] %02X:%02X:%02X:%02X:%02X:%02X\n",
           result->addr[0], result->addr[1], result->addr[2],
           result->addr[3], result->addr[4], result->addr[5]);
    if (memcmp(result->addr, CONTROLLER_ADDR, 6) == 0) {
        printf("[SCAN] Controller found! Connecting...\n");
        sle_connect(result->addr);
    }
}
```

- [ ] **Step 2: 编译烧录，观察连接回调**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e && ws63flash --flash /dev/ttyUSB0 standard-bs21e-1100e_all_in_one.fwpkg -b460800
```

- [ ] **Step 3: 记录连接状态**

串口输出中观察 `[CONN]` 回调：
- `state` 值含义（查阅 SDK 头文件中的 `sle_connect_state_t` 枚举）
- 如果连接失败，记录 `reason` 错误码

- [ ] **Step 4: 如果连接成功，查询连接参数**

在 `sle_manager.c` 中实现 `sle_get_connect_role()` 和 `sle_update_connect_param()` 调用，打印连接角色和参数。

- [ ] **Step 5: 提交**

```bash
git add wireless/bs21/src/
git commit -m "feat: connect to controller by hardcoded SLE address"
```

---

### Task 10: P1.3 - 配对手柄

**前提**: Task 9 完成，连接已建立。

**Files:**
- Modify: `wireless/bs21/src/main.c`

- [ ] **Step 1: 连接成功后自动配对**

已在 Task 7 中实现 `on_connect_state` 中 `state==1` 时调用 `sle_pair()`。确认此逻辑在手柄连接场景下同样触发。

- [ ] **Step 2: 编译烧录，观察配对回调**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e && ws63flash --flash /dev/ttyUSB0 standard-bs21e-1100e_all_in_one.fwpkg -b460800
```

- [ ] **Step 3: 记录配对结果**

串口输出中观察 `[PAIR]` 回调：
- `status` 值含义（0=成功，其他=错误码）
- 如果配对失败，分析错误码

- [ ] **Step 4: 如果配对失败，尝试 SMP 密钥**

在 `sle_manager.c` 中实现 `sle_set_nv_smp_keys()` 调用：
- 尝试设置不同的 SMP 密钥组合
- 记录每次尝试的结果

- [ ] **Step 5: 提交**

```bash
git add wireless/bs21/src/
git commit -m "feat: pair with controller, attempt SMP key setup"
```

---

## P1 完成检查点

**验证清单**:
- [ ] BS21 能扫描到手柄 SLE 广播
- [ ] 记录了手柄 SLE 地址、广播模式、广播数据
- [ ] BS21 与手柄建立 SLE 连接（state=1）
- [ ] 配对结果已记录（成功/失败 + 错误码）

**P1 通过（连接建立）后继续 P2，否则项目终止。**

---

### Task 11: P2.1 - 发送 NewXInput 初始化命令

**前提**: P1 通过，连接/配对已建立。

**Files:**
- Modify: `wireless/bs21/src/main.c`
- Create: `wireless/bs21/src/sle_parser.h`
- Create: `wireless/bs21/src/sle_parser.c`

**说明**: 假设 SLE 端应用层协议与 USB 端一致（5A A5 魔数），在 P2.1 中验证。

- [ ] **Step 1: 创建 sle_parser.h 接口**

```c
// wireless/bs21/src/sle_parser.h
#ifndef SLE_PARSER_H
#define SLE_PARSER_H

#include <stdint.h>
#include "controller_state.h"

typedef enum {
    PARSE_OK = 0,
    PARSE_NEED_MORE_DATA,
    PARSE_BAD_MAGIC,
    PARSE_BAD_LENGTH,
    PARSE_BAD_CHECKSUM,
} parse_result_t;

parse_result_t sle_parse_frame(const uint8_t *data, uint16_t len,
                               controller_state *out);

void sle_parser_print_hex(const uint8_t *data, uint16_t len);

#endif
```

- [ ] **Step 2: 创建 sle_parser.c 骨架实现**

```c
// wireless/bs21/src/sle_parser.c
#include "sle_parser.h"
#include <stdio.h>
#include <string.h>

parse_result_t sle_parse_frame(const uint8_t *data, uint16_t len,
                               controller_state *out)
{
    (void)data;
    (void)len;
    memset(out, 0, sizeof(*out));
    return PARSE_OK;
}

void sle_parser_print_hex(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        printf("%02X ", data[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    if (len % 16 != 0) printf("\n");
}
```

- [ ] **Step 3: 在 main.c 中发送初始化命令**

```c
// 连接成功后发送 NewXInput 初始化序列
static const uint8_t init_seq[] = {
    0x5A, 0xA5, 0x01, 0x02, 0x03,       // GetDeviceInfo
    0x5A, 0xA5, 0xA1, 0x02, 0xA3,       // GetSerialNumber
    0x5A, 0xA5, 0x02, 0x02, 0x04,       // ReadConfig
};
sle_send(conn_id, init_seq, sizeof(init_seq));
```

- [ ] **Step 4: 编译烧录，观察手柄响应**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e && ws63flash --flash /dev/ttyUSB0 standard-bs21e-1100e_all_in_one.fwpkg -b460800
```

- [ ] **Step 5: 提交**

```bash
git add wireless/bs21/src/
git commit -m "feat: send NewXInput init commands to controller"
```

---

### Task 12: P2.2 - 数据接收与字节映射

**前提**: Task 11 完成，手柄有数据响应。

**Files:**
- Modify: `wireless/bs21/src/sle_parser.c`
- Modify: `wireless/bs21/src/main.c`

- [ ] **Step 1: 在数据接收回调中打印原始 hex**

```c
static void on_data_received(uint8_t conn_id, const uint8_t *data, uint16_t len)
{
    printf("[DATA] conn_id=%u len=%u\n", conn_id, len);
    sle_parser_print_hex(data, len);
}
```

- [ ] **Step 2: 收集至少 100 帧数据，保存到文件**

将串口输出保存到 `wireless/bs21/tests/raw_frames.txt`。

- [ ] **Step 3: 逐字节对比 USB 20 字节标准报告**

在 `sle_parser.c` 中实现 `sle_parse_standard_report()`：

```c
static parse_result_t sle_parse_standard_report(const uint8_t *data, uint16_t len,
                                                 controller_state *out)
{
    if (len < 20) return PARSE_NEED_MORE_DATA;

    out->buttons.dpad_up    = (data[2] >> 0) & 0x01;
    out->buttons.dpad_down  = (data[2] >> 1) & 0x01;
    out->buttons.dpad_left  = (data[2] >> 2) & 0x01;
    out->buttons.dpad_right = (data[2] >> 3) & 0x01;
    out->buttons.start      = (data[2] >> 4) & 0x01;
    out->buttons.select     = (data[2] >> 5) & 0x01;
    out->buttons.l3         = (data[2] >> 6) & 0x01;
    out->buttons.r3         = (data[2] >> 7) & 0x01;

    out->buttons.a = (data[3] >> 0) & 0x01;
    out->buttons.b = (data[3] >> 1) & 0x01;
    out->buttons.x = (data[3] >> 4) & 0x01;
    out->buttons.y = (data[3] >> 5) & 0x01;
    out->buttons.lb = (data[3] >> 6) & 0x01;
    out->buttons.rb = (data[3] >> 7) & 0x01;
    out->buttons.home = (data[3] >> 2) & 0x01;

    out->lt = data[4];
    out->rt = data[5];

    out->lx = (int16_t)(data[6] | (data[7] << 8));
    out->ly = (int16_t)(data[8] | (data[9] << 8));
    out->rx = (int16_t)(data[10] | (data[11] << 8));
    out->ry = (int16_t)(data[12] | (data[13] << 8));

    return PARSE_OK;
}
```

- [ ] **Step 4: 更新 sle_parse_frame 调用标准报告解析**

```c
parse_result_t sle_parse_frame(const uint8_t *data, uint16_t len,
                               controller_state *out)
{
    return sle_parse_standard_report(data, len, out);
}
```

- [ ] **Step 5: 格式化输出解析结果验证**

```c
static void print_controller_state(const controller_state *state)
{
    printf("[STATE] LX=%d LY=%d RX=%d RY=%d LT=%u RT=%u\n",
           state->lx, state->ly, state->rx, state->ry,
           state->lt, state->rt);
    printf("[STATE] A=%d B=%d X=%d Y=%d LB=%d RB=%d\n",
           state->buttons.a, state->buttons.b, state->buttons.x,
           state->buttons.y, state->buttons.lb, state->buttons.rb);
}
```

- [ ] **Step 6: 编译烧录，验证解析结果与手柄操作一致**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e && ws63flash --flash /dev/ttyUSB0 standard-bs21e-1100e_all_in_one.fwpkg -b460800
```

移动摇杆、按下按键，确认串口输出正确反映操作。

- [ ] **Step 7: 提交**

```bash
git add wireless/bs21/src/
git commit -m "feat: parse standard 20-byte controller report"
```

---

### Task 13: P2.2 - 开启第三方控制模式 + 扩展报告解析

**前提**: Task 12 完成，标准报告解析正确。

**Files:**
- Modify: `wireless/bs21/src/sle_parser.c`
- Modify: `wireless/bs21/src/main.c`

- [ ] **Step 1: 发送 ThirdPartyControl 命令**

```c
static const uint8_t third_party_on[] = {
    0x5A, 0xA5, 0x11, 0x02, 0x01, 0x01,  // Command 0x11, enable
};
sle_send(conn_id, third_party_on, sizeof(third_party_on));
```

- [ ] **Step 2: 在 sle_parser.c 中实现扩展报告解析**

```c
static parse_result_t sle_parse_extended_report(const uint8_t *data, uint16_t len,
                                                 controller_state *out)
{
    if (len < 32) return PARSE_NEED_MORE_DATA;
    if (data[0] != 0x5A || data[1] != 0xA5 || data[2] != 0xEF)
        return PARSE_BAD_MAGIC;

    out->lx = (int16_t)(data[3] | (data[4] << 8));
    out->ly = (int16_t)(data[5] | (data[6] << 8));
    out->rx = (int16_t)(data[7] | (data[8] << 8));
    out->ry = (int16_t)(data[9] | (data[10] << 8));

    out->buttons.dpad_up    = (data[11] >> 0) & 0x01;
    out->buttons.dpad_down  = (data[11] >> 1) & 0x01;
    out->buttons.a         = (data[11] >> 4) & 0x01;
    out->buttons.b         = (data[11] >> 5) & 0x01;
    out->buttons.select    = (data[11] >> 6) & 0x01;
    out->buttons.x         = (data[11] >> 7) & 0x01;

    out->buttons.y      = (data[12] >> 0) & 0x01;
    out->buttons.start  = (data[12] >> 1) & 0x01;
    out->buttons.lb     = (data[12] >> 2) & 0x01;
    out->buttons.rb     = (data[12] >> 3) & 0x01;
    out->buttons.l3     = (data[12] >> 4) & 0x01;
    out->buttons.r3     = (data[12] >> 5) & 0x01;

    out->lt = data[15];
    out->rt = data[16];

    out->gyro_x = (int16_t)(data[17] | (data[18] << 8));
    out->gyro_y = (int16_t)(data[19] | (data[20] << 8));
    out->gyro_z = (int16_t)(data[21] | (data[22] << 8));
    out->accel_x = (int16_t)(data[23] | (data[24] << 8));
    out->accel_y = (int16_t)(data[25] | (data[26] << 8));
    out->accel_z = (int16_t)(data[27] | (data[28] << 8));

    return PARSE_OK;
}
```

- [ ] **Step 3: 更新 sle_parse_frame 自动判断报告类型**

```c
parse_result_t sle_parse_frame(const uint8_t *data, uint16_t len,
                               controller_state *out)
{
    if (len >= 3 && data[0] == 0x5A && data[1] == 0xA5 && data[2] == 0xEF)
        return sle_parse_extended_report(data, len, out);
    else
        return sle_parse_standard_report(data, len, out);
}
```

- [ ] **Step 4: 编译烧录，验证扩展报告（IMU 数据）**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e && ws63flash --flash /dev/ttyUSB0 standard-bs21e-1100e_all_in_one.fwpkg -b460800
```

移动手柄，确认陀螺仪/加速度计数据有变化。

- [ ] **Step 5: 提交**

```bash
git add wireless/bs21/src/
git commit -m "feat: add third-party control mode and extended report parsing"
```

---

### Task 14: P2.3 - 调整字段偏移 + 单元测试

**前提**: Task 13 完成，扩展报告解析基本正确。

**Files:**
- Modify: `wireless/bs21/src/sle_parser.c`
- Create: `wireless/bs21/tests/test_parser.c`
- Modify: `wireless/bs21/CMakeLists.txt`

- [ ] **Step 1: 对比实际 SLE 数据与 USB 数据，调整偏移**

如果 SLE 端字段偏移与 USB 端不同，修改 `sle_parser.c` 中的偏移常量。

- [ ] **Step 2: 创建 test_parser.c 单元测试**

```c
// wireless/bs21/tests/test_parser.c
#include <stdio.h>
#include <string.h>
#include "../src/sle_parser.h"
#include "../src/controller_state.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_EQ(a, b, msg) do { \
    if ((a) == (b)) { tests_passed++; } \
    else { printf("FAIL: %s: expected %d, got %d\n", msg, (int)(b), (int)(a)); tests_failed++; } \
} while(0)

static void test_standard_report(void)
{
    uint8_t raw[20] = {
        0x00, 0x14, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00
    };
    controller_state state;
    parse_result_t r = sle_parse_frame(raw, sizeof(raw), &state);
    ASSERT_EQ(r, PARSE_OK, "standard report parse ok");
    ASSERT_EQ(state.lx, 0, "lx default");
    ASSERT_EQ(state.ly, 0, "ly default");
}

static void test_extended_report_magic(void)
{
    uint8_t raw[32] = {0};
    raw[0] = 0x5A; raw[1] = 0xA5; raw[2] = 0xEF;
    raw[3] = 0x34; raw[4] = 0x12; // LX = 0x1234
    raw[5] = 0x78; raw[6] = 0x56; // LY = 0x5678
    controller_state state;
    parse_result_t r = sle_parse_frame(raw, sizeof(raw), &state);
    ASSERT_EQ(r, PARSE_OK, "extended report parse ok");
    ASSERT_EQ(state.lx, 0x1234, "lx value");
    ASSERT_EQ(state.ly, 0x5678, "ly value");
}

static void test_bad_magic(void)
{
    uint8_t raw[32] = {0};
    controller_state state;
    parse_result_t r = sle_parse_frame(raw, sizeof(raw), &state);
    ASSERT_EQ(r, PARSE_BAD_MAGIC, "bad magic detected");
}

static void test_need_more_data(void)
{
    uint8_t raw[5] = {0};
    controller_state state;
    parse_result_t r = sle_parse_frame(raw, sizeof(raw), &state);
    ASSERT_EQ(r, PARSE_NEED_MORE_DATA, "need more data");
}

int main(void)
{
    printf("=== sle_parser tests ===\n");
    test_standard_report();
    test_extended_report_magic();
    test_bad_magic();
    test_need_more_data();
    printf("Passed: %d, Failed: %d\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
```

- [ ] **Step 3: 更新 CMakeLists.txt 添加测试目标**

```cmake
# 追加到 CMakeLists.txt
enable_testing()
add_executable(test_parser tests/test_parser.c src/sle_parser.c)
add_test(NAME parser_test COMMAND test_parser)
```

- [ ] **Step 4: 编译并运行测试**

```bash
cd wireless/bs21 && mkdir -p build && cd build && cmake .. && make && ctest --output-on-failure
```

Expected: 4 tests passed, 0 failed.

- [ ] **Step 5: 提交**

```bash
git add wireless/bs21/
git commit -m "test: add sle_parser unit tests"
```

---

## P2 完成检查点

**验证清单**:
- [ ] 手柄响应初始化命令
- [ ] 标准 20 字节报告解析正确（按键/摇杆/扳机）
- [ ] 第三方控制模式开启后收到 32 字节扩展报告
- [ ] 扩展报告包含 IMU 数据
- [ ] 单元测试全部通过

---

### Task 15: P3.1 - USB CDC 配置

**前提**: P2 通过，数据解析正常。

**Files:**
- Create: `wireless/bs21/src/usb_cdc.h`
- Create: `wireless/bs21/src/usb_cdc.c`
- Modify: `wireless/bs21/prj.conf`

- [ ] **Step 1: 查阅 BS21 SDK USB CDC 示例**

```bash
find ~/fbb_bs2x/ -path "*/usb/cdc*" -name "*.c" | head -5
```

- [ ] **Step 2: 创建 usb_cdc.h 接口**

```c
// wireless/bs21/src/usb_cdc.h
#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdint.h>

void cdc_init(void);
void cdc_write(const uint8_t *data, uint16_t len);
int cdc_read(uint8_t *buf, uint16_t max_len);

#endif
```

- [ ] **Step 3: 创建 usb_cdc.c 骨架实现**

```c
// wireless/bs21/src/usb_cdc.c
#include "usb_cdc.h"
#include <stdio.h>

void cdc_init(void)
{
    printf("[CDC] init\n");
}

void cdc_write(const uint8_t *data, uint16_t len)
{
    printf("[CDC] write %u bytes\n", len);
}

int cdc_read(uint8_t *buf, uint16_t max_len)
{
    (void)buf;
    (void)max_len;
    return 0;
}
```

- [ ] **Step 4: 更新 prj.conf 启用 USB CDC**

```
CONFIG_USB=y
CONFIG_USB_DEVICE=y
CONFIG_USB_CDC_ACM=y
```

- [ ] **Step 5: 编译烧录，验证 USB 设备出现**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e && ws63flash --flash /dev/ttyUSB0 standard-bs21e-1100e_all_in_one.fwpkg -b460800
```

USB1 接 PC，验证 `/dev/ttyACM*` 出现。

- [ ] **Step 6: 提交**

```bash
git add wireless/bs21/src/usb_cdc.h wireless/bs21/src/usb_cdc.c wireless/bs21/prj.conf
git commit -m "feat: add USB CDC skeleton"
```

---

### Task 16: P3.1 - 接入真实 USB CDC SDK API

**前提**: Task 15 完成，USB CDC 骨架通过。

**Files:**
- Modify: `wireless/bs21/src/usb_cdc.c`

- [ ] **Step 1: 查阅 SDK USB CDC API**

```bash
cat ~/fbb_bs2x/.../usb_cdc_acm.h
```

- [ ] **Step 2: 替换 cdc_init() 为真实实现**

在 `usb_cdc.c` 中替换为：
- USB 设备初始化
- CDC ACM 类注册
- 配置端点

- [ ] **Step 3: 替换 cdc_write() 为真实实现**

使用 BS21 SDK 的 CDC 发送函数。

- [ ] **Step 4: 编译烧录，串口工具连接验证**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e && ws63flash --flash /dev/ttyUSB0 standard-bs21e-1100e_all_in_one.fwpkg -b460800
```

用 `screen /dev/ttyACM0 115200` 或 `minicom` 连接，验证能收发数据。

- [ ] **Step 5: 提交**

```bash
git add wireless/bs21/src/usb_cdc.c
git commit -m "feat: integrate real USB CDC SDK API"
```

---

### Task 17: P3.2 - 文本 Formatter

**前提**: Task 16 完成，CDC 可收发数据。

**Files:**
- Create: `wireless/bs21/src/formatter_text.h`
- Create: `wireless/bs21/src/formatter_text.c`
- Modify: `wireless/bs21/src/main.c`

- [ ] **Step 1: 创建 formatter_text.h 接口**

```c
// wireless/bs21/src/formatter_text.h
#ifndef FORMATTER_TEXT_H
#define FORMATTER_TEXT_H

#include <stdint.h>
#include "controller_state.h"

uint16_t format_text(const controller_state *state, char *buf, uint16_t buf_size);

#endif
```

- [ ] **Step 2: 创建 formatter_text.c 实现**

```c
// wireless/bs21/src/formatter_text.c
#include "formatter_text.h"
#include <stdio.h>

uint16_t format_text(const controller_state *state, char *buf, uint16_t buf_size)
{
    return snprintf(buf, buf_size,
        "LX=%+5d LY=%+5d RX=%+5d RY=%+5d LT=%3u RT=%3u "
        "BTN: A=%d B=%d X=%d Y=%d LB=%d RB=%d "
        "DPAD: U=%d D=%d L=%d R=%d "
        "START=%d SELECT=%d L3=%d R3=%d HOME=%d\n",
        state->lx, state->ly, state->rx, state->ry,
        state->lt, state->rt,
        state->buttons.a, state->buttons.b, state->buttons.x,
        state->buttons.y, state->buttons.lb, state->buttons.rb,
        state->buttons.dpad_up, state->buttons.dpad_down,
        state->buttons.dpad_left, state->buttons.dpad_right,
        state->buttons.start, state->buttons.select,
        state->buttons.l3, state->buttons.r3, state->buttons.home);
}
```

- [ ] **Step 3: 在 main.c 数据接收回调中集成 CDC 输出**

```c
static void on_data_received(uint8_t conn_id, const uint8_t *data, uint16_t len)
{
    controller_state state;
    parse_result_t r = sle_parse_frame(data, len, &state);
    if (r == PARSE_OK) {
        char buf[256];
        uint16_t n = format_text(&state, buf, sizeof(buf));
        cdc_write((const uint8_t *)buf, n);
    }
}
```

- [ ] **Step 4: 在 main.c 中初始化 CDC**

```c
int main(void)
{
    cdc_init();
    sle_init();
    sle_set_scan_callback(on_scan_result);
    // ...
}
```

- [ ] **Step 5: 编译烧录，端到端验证**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e && ws63flash --flash /dev/ttyUSB0 standard-bs21e-1100e_all_in_one.fwpkg -b460800
```

手柄操作 → CDC 输出文本行，确认按键/摇杆/扳机正确。

- [ ] **Step 6: 提交**

```bash
git add wireless/bs21/src/
git commit -m "feat: add text formatter with CDC output"
```

---

### Task 18: P3.3 - 二进制 Formatter

**前提**: Task 17 完成，文本输出正常。

**Files:**
- Create: `wireless/bs21/src/formatter_binary.h`
- Create: `wireless/bs21/src/formatter_binary.c`

- [ ] **Step 1: 创建 formatter_binary.h 接口**

```c
// wireless/bs21/src/formatter_binary.h
#ifndef FORMATTER_BINARY_H
#define FORMATTER_BINARY_H

#include <stdint.h>
#include "controller_state.h"

#define BINARY_FRAME_MAGIC 0xFD01
#define BINARY_FRAME_MAX_SIZE 128

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint16_t length;
    uint8_t  sequence;
    controller_state state;
    uint16_t crc;
} binary_frame_t;

uint16_t format_binary(const controller_state *state, uint8_t *buf, uint16_t buf_size);

#endif
```

- [ ] **Step 2: 创建 formatter_binary.c 实现**

```c
// wireless/bs21/src/formatter_binary.c
#include "formatter_binary.h"
#include <string.h>

static uint16_t crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xA001;
            else crc >>= 1;
        }
    }
    return crc;
}

uint16_t format_binary(const controller_state *state, uint8_t *buf, uint16_t buf_size)
{
    if (buf_size < sizeof(binary_frame_t)) return 0;

    binary_frame_t *frame = (binary_frame_t *)buf;
    memset(frame, 0, sizeof(*frame));

    frame->magic = BINARY_FRAME_MAGIC;
    frame->length = sizeof(controller_state);
    frame->state = *state;
    frame->crc = crc16((const uint8_t *)&frame->state, frame->length);

    return sizeof(binary_frame_t);
}
```

- [ ] **Step 3: 编译验证**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e
```

- [ ] **Step 4: 提交**

```bash
git add wireless/bs21/src/formatter_binary.h wireless/bs21/src/formatter_binary.c
git commit -m "feat: add binary formatter with CRC16"
```

---

### Task 19: P3.3 - PC 端 Python 二进制解析脚本

**前提**: Task 18 完成。

**Files:**
- Create: `wireless/bs21/scripts/parse_binary.py`

- [ ] **Step 1: 创建 parse_binary.py**

```python
#!/usr/bin/env python3
"""Parse binary frames from BS21 CDC serial output."""

import struct
import sys
import serial
import time

MAGIC = 0xFD01
FRAME_SIZE = 128  # sizeof(binary_frame_t)

def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF

def parse_frame(data):
    magic, length, seq = struct.unpack_from('<HHB', data, 0)
    if magic != MAGIC:
        return None
    state_bytes = data[5:5+length]
    expected_crc = struct.unpack_from('<H', data, 5+length)[0]
    if crc16(state_bytes) != expected_crc:
        print(f"CRC mismatch", file=sys.stderr)
        return None

    # controller_state layout (packed, little-endian)
    fields = struct.unpack_from('<hhhhBBBBBBBBBBhhhhhh', state_bytes, 0)
    (lx, ly, rx, ry, lt, rt,
     a, b, x, y, lb, rb, home,
     dpad_up, dpad_down, dpad_left, dpad_right,
     start, select, l3, r3,
     gyro_x, gyro_y, gyro_z, accel_x, accel_y, accel_z) = fields

    return {
        'seq': seq,
        'lx': lx, 'ly': ly, 'rx': rx, 'ry': ry,
        'lt': lt, 'rt': rt,
        'buttons': {
            'a': a, 'b': b, 'x': x, 'y': y,
            'lb': lb, 'rb': rb, 'home': home,
            'dpad_up': dpad_up, 'dpad_down': dpad_down,
            'dpad_left': dpad_left, 'dpad_right': dpad_right,
            'start': start, 'select': select,
            'l3': l3, 'r3': r3,
        },
        'gyro': (gyro_x, gyro_y, gyro_z),
        'accel': (accel_x, accel_y, accel_z),
    }

def find_port():
    import glob, os
    for dev in glob.glob('/dev/ttyACM*'):
        link = f'/sys/class/tty/{os.path.basename(dev)}/device/interface'
        try:
            if open(link).read().strip():
                return dev
        except FileNotFoundError:
            pass
    return None

def main():
    port = find_port()
    if not port:
        print("No CDC port found", file=sys.stderr)
        sys.exit(1)

    print(f"Connecting to {port}...")
    ser = serial.Serial(port, 115200, timeout=1)

    buf = bytearray()
    while True:
        buf.extend(ser.read(1024))
        while len(buf) >= FRAME_SIZE:
            frame = parse_frame(buf[:FRAME_SIZE])
            buf = buf[FRAME_SIZE:]
            if frame:
                s = frame
                print(f"[{s['seq']:03d}] "
                      f"LX={s['lx']:+5d} LY={s['ly']:+5d} "
                      f"RX={s['rx']:+5d} RY={s['ry']:+5d} "
                      f"LT={s['lt']:3d} RT={s['rt']:3d} "
                      f"A={s['buttons']['a']} B={s['buttons']['b']} "
                      f"X={s['buttons']['x']} Y={s['buttons']['y']}")

if __name__ == '__main__':
    main()
```

- [ ] **Step 2: 测试 Python 脚本**

```bash
python3 wireless/bs21/scripts/parse_binary.py
```

Expected: 连接 CDC 端口，持续输出解析后的手柄数据。

- [ ] **Step 3: 提交**

```bash
git add wireless/bs21/scripts/parse_binary.py
git commit -m "feat: add PC-side Python binary frame parser"
```

---

## P3 完成检查点

**验证清单**:
- [ ] USB CDC 设备可在 PC 端识别
- [ ] 文本格式输出正确反映手柄操作
- [ ] 二进制格式输出通过 Python 脚本正确解析
- [ ] CRC16 校验正确

---

### Task 20: P4.1 - USB HID 配置

**前提**: P3 通过，CDC 输出正常。

**Files:**
- Create: `wireless/bs21/src/hid_mapper.h`
- Create: `wireless/bs21/src/hid_mapper.c`
- Modify: `wireless/bs21/prj.conf`

- [ ] **Step 1: 查阅 BS21 SDK USB HID 示例**

```bash
find ~/fbb_bs2x/ -path "*/usb/hid*" -name "*.c" | head -5
```

- [ ] **Step 2: 创建 hid_mapper.h 接口**

```c
// wireless/bs21/src/hid_mapper.h
#ifndef HID_MAPPER_H
#define HID_MAPPER_H

#include <stdint.h>
#include "controller_state.h"

#define XBOX_HID_REPORT_SIZE 20

uint16_t map_to_xbox(const controller_state *state, uint8_t *report);

#endif
```

- [ ] **Step 3: 创建 hid_mapper.c 实现**

```c
// wireless/bs21/src/hid_mapper.c
#include "hid_mapper.h"
#include <string.h>

uint16_t map_to_xbox(const controller_state *state, uint8_t *report)
{
    memset(report, 0, XBOX_HID_REPORT_SIZE);

    report[0] = 0x00;  // report ID

    // buttons byte 1
    if (state->buttons.dpad_up)    report[2] |= 0x01;
    if (state->buttons.dpad_down)  report[2] |= 0x02;
    if (state->buttons.dpad_left)  report[2] |= 0x04;
    if (state->buttons.dpad_right) report[2] |= 0x08;
    if (state->buttons.start)      report[2] |= 0x10;
    if (state->buttons.select)     report[2] |= 0x20;
    if (state->buttons.l3)         report[2] |= 0x40;
    if (state->buttons.r3)         report[2] |= 0x80;

    // buttons byte 2
    if (state->buttons.a)  report[3] |= 0x10;
    if (state->buttons.b)  report[3] |= 0x20;
    if (state->buttons.x)  report[3] |= 0x40;
    if (state->buttons.y)  report[3] |= 0x80;
    if (state->buttons.lb) report[3] |= 0x01;
    if (state->buttons.rb) report[3] |= 0x02;

    // triggers (uint8 → uint16, scale to 0-1023)
    report[4] = (uint8_t)(state->lt * 4);
    report[5] = (uint8_t)(state->rt * 4);

    // left stick (int16 → int16, clamp)
    int16_t lx = state->lx;
    report[6] = (uint8_t)(lx & 0xFF);
    report[7] = (uint8_t)((lx >> 8) & 0xFF);

    int16_t ly = state->ly;
    report[8] = (uint8_t)(ly & 0xFF);
    report[9] = (uint8_t)((ly >> 8) & 0xFF);

    // right stick
    int16_t rx = state->rx;
    report[10] = (uint8_t)(rx & 0xFF);
    report[11] = (uint8_t)((rx >> 8) & 0xFF);

    int16_t ry = state->ry;
    report[12] = (uint8_t)(ry & 0xFF);
    report[13] = (uint8_t)((ry >> 8) & 0xFF);

    return XBOX_HID_REPORT_SIZE;
}
```

- [ ] **Step 4: 更新 prj.conf 启用 USB HID**

```
CONFIG_USB_HID=y
CONFIG_USB_HID_DEVICE=y
```

- [ ] **Step 5: 编译烧录，验证设备识别**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e && ws63flash --flash /dev/ttyUSB0 standard-bs21e-1100e_all_in_one.fwpkg -b460800
```

USB1 接 PC，验证 `lsusb` 或设备管理器显示 "Xbox 360 Controller"。

- [ ] **Step 6: 提交**

```bash
git add wireless/bs21/src/hid_mapper.h wireless/bs21/src/hid_mapper.c wireless/bs21/prj.conf
git commit -m "feat: add USB HID Xbox controller mapper"
```

---

### Task 21: P4.1 - Xbox HID 报告描述符

**前提**: Task 20 完成，HID 设备可识别但功能不完整。

**Files:**
- Modify: `wireless/bs21/src/main.c` (或 USB HID 配置文件)

- [ ] **Step 1: 编写 Xbox 360 手柄 HID 报告描述符**

```c
// Xbox 360 Controller HID Report Descriptor
static const uint8_t xbox_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x35, 0x00,        //   Physical Minimum (0)
    0x45, 0x01,        //   Physical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x0E,        //   Report Count (14)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (1)
    0x29, 0x0E,        //   Usage Maximum (14)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)

    0x95, 0x02,        //   Report Count (2)
    0x81, 0x01,        //   Input (Constant)

    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x25, 0x07,        //   Logical Maximum (7)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x65, 0x14,        //   Unit (Degrees)
    0x09, 0x39,        //   Usage (Hat Switch)
    0x81, 0x42,        //   Input (Data, Variable, Absolute, Null)

    0x65, 0x00,        //   Unit (None)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x01,        //   Input (Constant)

    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x46, 0xFF, 0x00,  //   Physical Maximum (255)
    0x09, 0x30,        //   Usage (X)
    0x09, 0x31,        //   Usage (Y)
    0x09, 0x32,        //   Usage (Z)
    0x09, 0x35,        //   Usage (Rz)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)

    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x01,        //   Input (Constant)

    0x05, 0x02,        //   Usage Page (Simulation Controls)
    0x09, 0xC5,        //   Usage (Brake)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x46, 0xFF, 0x00,  //   Physical Maximum (255)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)

    0x09, 0xC4,        //   Usage (Accelerator)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)

    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant)

    0xC0               // End Collection
};
```

- [ ] **Step 2: 注册 HID 报告描述符到 USB 栈**

在 USB HID 初始化代码中注册 `xbox_report_descriptor`。

- [ ] **Step 3: 编译烧录，验证 `jstest` 输出**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e && ws63flash --flash /dev/ttyUSB0 standard-bs21e-1100e_all_in_one.fwpkg -b460800
jstest /dev/input/js0
```

Expected: 按键和摇杆映射正确。

- [ ] **Step 4: 提交**

```bash
git add wireless/bs21/src/
git commit -m "feat: add Xbox 360 HID report descriptor"
```

---

### Task 22: P4.2 - HID 数据映射集成

**前提**: Task 21 完成，HID 设备功能正常。

**Files:**
- Modify: `wireless/bs21/src/main.c`

- [ ] **Step 1: 在 main.c 数据接收回调中集成 HID 输出**

```c
static void on_data_received(uint8_t conn_id, const uint8_t *data, uint16_t len)
{
    controller_state state;
    parse_result_t r = sle_parse_frame(data, len, &state);
    if (r == PARSE_OK) {
        uint8_t report[XBOX_HID_REPORT_SIZE];
        uint16_t report_len = map_to_xbox(&state, report);
        hid_send_report(report, report_len);
    }
}
```

- [ ] **Step 2: 编译烧录，验证游戏功能**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e && ws63flash --flash /dev/ttyUSB0 standard-bs21e-1100e_all_in_one.fwpkg -b460800
```

- [ ] **Step 3: 提交**

```bash
git add wireless/bs21/src/main.c
git commit -m "feat: integrate HID output into main loop"
```

---

### Task 23: P4.3 - 震动反馈

**前提**: Task 22 完成，基本输入正常。

**Files:**
- Modify: `wireless/bs21/src/main.c`
- Modify: `wireless/bs21/src/sle_manager.c`

- [ ] **Step 1: 添加 HID Output Report 回调**

在 USB HID 配置中注册输出报告回调，接收 PC 端震动命令。

- [ ] **Step 2: 解析震动命令**

```c
static void on_hid_output(const uint8_t *data, uint16_t len)
{
    if (len < 8) return;
    uint8_t left_motor  = data[3];  // low-frequency rumble
    uint8_t right_motor = data[4];  // high-frequency rumble

    // 构造震动命令 (NewXInput Command 0x12)
    uint8_t rumble_cmd[] = {
        0x5A, 0xA5, 0x12, 0x04, 0x06,
        left_motor, right_motor, 0x00, 0x00
    };
    sle_send(conn_id, rumble_cmd, sizeof(rumble_cmd));
}
```

- [ ] **Step 3: 编译烧录，测试震动**

```bash
cd ~/fbb_bs2x/src && python3 build.py standard-bs21e-1100e && ws63flash --flash /dev/ttyUSB0 standard-bs21e-1100e_all_in_one.fwpkg -b460800
```

使用 `fftest /dev/input/eventX` 或游戏测试震动反馈。

- [ ] **Step 4: 提交**

```bash
git add wireless/bs21/src/
git commit -m "feat: add rumble feedback via HID output report"
```

---

## P4 完成检查点

**验证清单**:
- [ ] PC 识别为 Xbox 360 Controller
- [ ] `jstest` / `evtest` 显示正确按键和摇杆
- [ ] 游戏可正常使用手柄
- [ ] 震动反馈可用

---

## 最终集成

### Task 24: 最终集成测试

- [ ] **Step 1: 端到端测试脚本**

创建 `wireless/bs21/tests/e2e_test.sh`：

```bash
#!/bin/bash
# End-to-end test: controller → BS21 → USB HID → PC
echo "=== BS21 Receiver E2E Test ==="
echo "1. Connect BS21 board USB1 to PC"
echo "2. Power on controller in PC mode"
echo "3. Check dmesg for Xbox 360 Controller"
dmesg | tail -20 | grep -i xbox
echo "4. Run jstest"
jstest /dev/input/js0 &
JSTEST_PID=$!
sleep 10
kill $JSTEST_PID
echo "=== Test complete ==="
```

- [ ] **Step 2: 运行端到端测试**

```bash
bash wireless/bs21/tests/e2e_test.sh
```

- [ ] **Step 3: 提交**

```bash
git add wireless/bs21/tests/e2e_test.sh
git commit -m "test: add end-to-end test script"
```

---

## 任务汇总

| Task | 阶段 | 内容 | 预计 |
|------|------|------|------|
| 1 | P0.1 | 创建项目骨架 + Hello World | 30min |
| 2 | P0.2 | 定义 controller_state.h | 10min |
| 3 | P0.3 | 双板互验：T 节点广播 | 25min |
| 4 | P0.3 | 双板互验：G 节点扫描 | 15min |
| 5 | P0.3 | 接入真实 SLE SDK API | 30min |
| 6 | P0.3 | 双板 SLE 连接 | 20min |
| 7 | P0.3 | 双板 SLE 配对 + SSAP 收发 | 25min |
| 8 | P1.1 | 扫描手柄 SLE 广播 | 20min |
| 9 | P1.2 | 连接手柄 | 20min |
| 10 | P1.3 | 配对手柄 | 20min |
| 11 | P2.1 | 发送 NewXInput 初始化命令 | 20min |
| 12 | P2.2 | 数据接收与字节映射 | 30min |
| 13 | P2.2 | 第三方控制 + 扩展报告解析 | 25min |
| 14 | P2.3 | 调整字段偏移 + 单元测试 | 25min |
| 15 | P3.1 | USB CDC 骨架 | 15min |
| 16 | P3.1 | 接入真实 USB CDC API | 20min |
| 17 | P3.2 | 文本 Formatter | 20min |
| 18 | P3.3 | 二进制 Formatter | 15min |
| 19 | P3.3 | PC 端 Python 解析脚本 | 15min |
| 20 | P4.1 | USB HID 配置 + mapper | 20min |
| 21 | P4.1 | Xbox HID 报告描述符 | 20min |
| 22 | P4.2 | HID 数据映射集成 | 10min |
| 23 | P4.3 | 震动反馈 | 20min |
| 24 | 集成 | 端到端测试 | 10min |

**总计: 24 tasks, ~8h**

## 关键决策点

- **Task 7 后**: P0 完成检查点 — 双板 SLE 互验通过？
- **Task 10 后**: P1 完成检查点 — 手柄连接成功？**失败则项目终止**
- **Task 14 后**: P2 完成检查点 — 协议解析正确？
- **Task 19 后**: P3 完成检查点 — CDC 输出正常？
- **Task 23 后**: P4 完成检查点 — USB HID 功能完整？
