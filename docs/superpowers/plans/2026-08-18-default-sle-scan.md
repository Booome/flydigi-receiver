# default SLE 扫描器 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `default` app 成为 SLE 扫描器：使能 SLE、持续扫描、按地址聚合统计、每 2 秒打印设备表，用于通过开关手柄识别手柄地址。

**Architecture:** 复用已验证的 `g_scanner` 扫描流程（enable_sle → seek 参数 → sle_start_seek → seek_result_cb），新增静态设备表（32 项）+ 定时打印任务。`gen-config.py` 需要给 default 启用 SLE central（`SUPPORT_SLE_CENTRAL` + `sle-central`），否则 SDK 默认配置下扫描不可用。

**Tech Stack:** C (RISC-V, Ai-BS21_SDK, 只读引用 SDK)、CMake、`tools/burn.py` 烧录、串口 115200 验证。

## Global Constraints

- 代码注释/命名用英文；禁止中文出现在代码里。
- SDK（`~/.local/Ai-BS21_SDK`）只读引用，绝不修改 SDK 源码。
- 只改两个项目文件：`wireless/bs21/scripts/gen-config.py` 和 `wireless/bs21/apps/default/main.c`。
- 构建（default）：`cmake -S wireless/bs21 -B wireless/bs21/build && cmake --build wireless/bs21/build -j`。
- 构建（t_broadcaster）：`cmake -B wireless/bs21/build/t_broadcaster -DBS21_APP=t_broadcaster`（已构建过，无需重建）。
- 烧录：`python3 wireless/bs21/tools/burn.py <board> -a <app>`（board_a/board_b）。
- 串口抓 log：`stty -F <port> 115200 raw -echo; cat <port> > /tmp/xx.log &`，复位用 `uart-gpio pulse <ctrl> A <pin> 0 3000`（board_a pin 8，board_b pin 11）。

---

### Task 1: gen-config.py 启用 default 的 SLE central

**Files:**
- Modify: `wireless/bs21/scripts/gen-config.py`

**Interfaces:**
- Consumes: 现有 `APP == 'g_scanner'` 分支的写法（已工作）。
- Produces: default 构建生成的 config.cmake 含 `SUPPORT_SLE_CENTRAL` 且 `CONFIG_SLE_BLE_SUPPORT="sle-central"`，使 default 能调用 seek API。

- [ ] **Step 1: 修改 gen-config.py**

把 `t_broadcaster` 分支后追加 default 分支（与 g_scanner 相同逻辑）：

```python
elif APP == 't_broadcaster':
    env.config['config_sle_ble_support'] = 'sle-peripheral'
elif APP == 'default':
    env.remove('defines', 'SUPPORT_SLE_PERIPHERAL')
    env.append('defines', 'SUPPORT_SLE_CENTRAL')
    env.config['config_sle_ble_support'] = 'sle-central'
```

- [ ] **Step 2: 重新 configure default 并验证生成配置**

Run:
```bash
cd /home/bodong/workspace/flydigi-receiver/.worktrees/default-sle-scan
cmake -S wireless/bs21 -B wireless/bs21/build
grep -E "SUPPORT_SLE_CENTRAL|CONFIG_SLE_BLE_SUPPORT" wireless/bs21/build/out.cmake
```
Expected: `SUPPORT_SLE_CENTRAL` 出现在 defines（`set(DEFINES "...")` 内），且 `set(CONFIG_SLE_BLE_SUPPORT "sle-central" ...)`。

> 注：生成文件名以实际 configure 输出为准（gen-config.py 写 `set(...)` 到 OUT，顶层 CMakeLists 可能重命名）。用 `grep -ri "sle-central\|SUPPORT_SLE_CENTRAL" wireless/bs21/build/ --include=*.cmake` 兜底查找。

- [ ] **Step 3: Commit**

```bash
git add wireless/bs21/scripts/gen-config.py
git commit -m "build: enable SLE central for default app"
```

---

### Task 2: 重写 default/main.c 为扫描器

**Files:**
- Modify: `wireless/bs21/apps/default/main.c`（整体替换，原 hello_task 逻辑删除）

**Interfaces:**
- Consumes: Task 1 的 central 配置；SDK API：
  - `sle_device_manager.h`: `sle_dev_manager_callbacks_t`, `sle_dev_manager_register_callbacks()`, `enable_sle()`
  - `sle_device_discovery.h`: `sle_seek_param_t`, `sle_seek_result_info_t`, `sle_announce_seek_callbacks_t`, `sle_announce_seek_register_callbacks()`, `sle_set_seek_param()`, `sle_start_seek()`
  - `sle_common.h`: `sle_addr_t`, `SLE_ADDR_LEN`
- Produces: `axk_main()` 启动扫描器；`seek_result_cb()` 聚合；`scan_task` 每 2 秒打印设备表。

- [ ] **Step 1: 写入完整 main.c**

```c
#include "soc_osal.h"
#include "pinctrl.h"
#include "gpio.h"
#include "chip_io.h"
#include "securec.h"
#include "string.h"
#include "errcode.h"
#include "sle_common.h"
#include "sle_device_manager.h"
#include "sle_device_discovery.h"

#define SCAN_TABLE_SIZE  32
#define SCAN_PRINT_MS    2000

typedef struct {
    sle_addr_t addr;
    int8_t rssi;
    uint32_t count;
    uint8_t used;
} scan_device_t;

static scan_device_t g_scan_table[SCAN_TABLE_SIZE] = { 0 };
static sle_dev_manager_callbacks_t g_dev_cbk = { 0 };
static sle_announce_seek_callbacks_t g_seek_cbk = { 0 };

static void bs21_rst(void)
{
    uapi_pin_set_mode(S_MGPIO21, (pin_mode_t)HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(S_MGPIO21, GPIO_DIRECTION_INPUT);
    uapi_pin_set_pull(S_MGPIO21, PIN_PULL_UP);
    reg16_setbits(0x5702C51C, 4, 5, 21);
    reg16_clrbit(0x5702C51C, 0);
}

static scan_device_t *table_find(const sle_addr_t *addr)
{
    for (uint8_t i = 0; i < SCAN_TABLE_SIZE; i++) {
        if (g_scan_table[i].used &&
            memcmp(g_scan_table[i].addr.addr, addr->addr, SLE_ADDR_LEN) == 0) {
            return &g_scan_table[i];
        }
    }
    return NULL;
}

static scan_device_t *table_add(const sle_addr_t *addr)
{
    for (uint8_t i = 0; i < SCAN_TABLE_SIZE; i++) {
        if (!g_scan_table[i].used) {
            g_scan_table[i].used = 1;
            (void)memcpy_s(g_scan_table[i].addr.addr, SLE_ADDR_LEN, addr->addr, SLE_ADDR_LEN);
            return &g_scan_table[i];
        }
    }
    osal_printk("[scan] table full\r\n");
    return NULL;
}

static void seek_result_cb(sle_seek_result_info_t *result)
{
    scan_device_t *dev;
    if (result == NULL) {
        return;
    }
    dev = table_find(&result->addr);
    if (dev == NULL) {
        dev = table_add(&result->addr);
        if (dev == NULL) {
            return;
        }
    }
    dev->count++;
    dev->rssi = result->rssi;
}

static void print_scan_table(void)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < SCAN_TABLE_SIZE; i++) {
        if (g_scan_table[i].used) {
            n++;
        }
    }
    osal_printk("[scan] devices:%u\r\n", n);
    for (uint8_t i = 0; i < SCAN_TABLE_SIZE; i++) {
        if (g_scan_table[i].used) {
            osal_printk("  %u) %02x:%02x:%02x:%02x:%02x:%02x rssi:%d cnt:%u\r\n",
                        i,
                        g_scan_table[i].addr.addr[0], g_scan_table[i].addr.addr[1],
                        g_scan_table[i].addr.addr[2], g_scan_table[i].addr.addr[3],
                        g_scan_table[i].addr.addr[4], g_scan_table[i].addr.addr[5],
                        g_scan_table[i].rssi, g_scan_table[i].count);
        }
    }
}

static void *scan_task(const char *arg)
{
    (void)arg;
    while (1) {
        osal_msleep(SCAN_PRINT_MS);
        print_scan_table();
    }
    return NULL;
}

static void sle_power_on_cb(uint8_t status)
{
    osal_printk("sle power on: %d\r\n", status);
    enable_sle();
}

static void sle_enable_cb(uint8_t status)
{
    osal_printk("sle enable: %d\r\n", status);
    sle_announce_seek_register_callbacks(&g_seek_cbk);
    scan_start();
}

static void scan_start(void)
{
    sle_seek_param_t param = { 0 };
    errcode_t rc;
    param.own_addr_type = 0;
    param.filter_duplicates = 0;
    param.seek_filter_policy = 0;
    param.seek_phys = 1;
    param.seek_type[0] = 1;
    param.seek_interval[0] = 100;
    param.seek_window[0] = 100;
    rc = sle_set_seek_param(&param);
    if (rc != ERRCODE_SUCC) {
        osal_printk("sle_set_seek_param fail: 0x%x\r\n", rc);
        return;
    }
    rc = sle_start_seek();
    if (rc != ERRCODE_SUCC) {
        osal_printk("sle_start_seek fail: 0x%x\r\n", rc);
    }
}

static void seek_enable_cb(errcode_t status)
{
    osal_printk("seek enable: 0x%x\r\n", status);
}

void axk_main(void)
{
    osal_printk("app: flydigi-wireless\r\n");
    bs21_rst();

    g_dev_cbk.sle_power_on_cb = sle_power_on_cb;
    g_dev_cbk.sle_enable_cb = sle_enable_cb;
    sle_dev_manager_register_callbacks(&g_dev_cbk);

    g_seek_cbk.seek_enable_cb = seek_enable_cb;
    g_seek_cbk.seek_result_cb = seek_result_cb;

    osal_task *task_handle = NULL;
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)scan_task, 0, "scan_task", 0x1000);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, 24);
        osal_kfree(task_handle);
    }
    osal_kthread_unlock();

    enable_sle();
}
```

- [ ] **Step 2: 编译 default，确认无错误**

Run:
```bash
cd /home/bodong/workspace/flydigi-receiver/.worktrees/default-sle-scan
cmake --build wireless/bs21/build -j 2>&1 | tail -20
```
Expected: `[100%] Built target package`，无编译错误。若报 `sle_*` 未定义，回到 Task 1 检查 config.cmake 是否含 `SUPPORT_SLE_CENTRAL`。

- [ ] **Step 3: 检查 fwpkg 含 patch 表（回归）**

Run:
```bash
python3 -c "
d = open('wireless/bs21/build/bs21_all_in_one.fwpkg','rb').read()
print('TBL:', bytes.fromhex('efa28239') in d)
print('CMP:', bytes.fromhex('000000000000040024000000') in d)
"
```
Expected: `TBL: True` 和 `CMP: True`（回归验证 GENERAT_SIGNBIN 修复对 default 仍生效）。

- [ ] **Step 4: Commit**

```bash
git add wireless/bs21/apps/default/main.c
git commit -m "feat: implement SLE scanner in default app"
```

---

### Task 3: 板对板验证扫描

**Files:** 无代码改动（烧录与观察）。

**Interfaces:**
- Consumes: Task 2 构建出的 `wireless/bs21/build/bs21_all_in_one.fwpkg`。
- Produces: 扫描到 `00:00:00:00:00:00`（board_a t_broadcaster）且 cnt 持续增长的串口证据。

- [ ] **Step 1: 烧录 board_b = default**

Run:
```bash
cd /home/bodong/workspace/flydigi-receiver/.worktrees/default-sle-scan
python3 wireless/bs21/tools/burn.py board_b -a default
```
Expected: `[flash] board_b flashed successfully!`（board_a 保持 t_broadcaster，不用动）。

- [ ] **Step 2: 复位 board_a 触发广播，抓 board_b 串口**

Run:
```bash
CTRL=/dev/serial/by-path/pci-0000:00:14.0-usb-0:3.4.1.1:1.0-port0
MODA=/dev/serial/by-path/pci-0000:00:14.0-usb-0:3.4.1.2:1.0-port0
MODB=/dev/serial/by-path/pci-0000:00:14.0-usb-0:3.4.1.3:1.0-port0
uart-gpio pulse $CTRL A 8 0 3000   # 复位 board_a 开始广播
pkill -f "cat $MODB"; sleep 0.3
stty -F $MODB 115200 raw -echo
cat $MODB > /tmp/opencode/default_scan.log &
sleep 0.5
uart-gpio pulse $CTRL A 11 0 3000  # 复位 board_b 触发扫描
sleep 8
kill %1
grep -aE "app: flydigi-wireless|sle enable|seek enable|\[scan\]" /tmp/opencode/default_scan.log
```
Expected: `app: flydigi-wireless`、`seek enable: 0x0`、出现 `[scan] devices:1` 且 `00:00:00:00:00:00 ... cnt:>`（第二次打印时 cnt 明显增大）。

- [ ] **Step 3: 记录结果**

在 `/tmp/opencode/default_scan.log` 中确认两个时间点的 `[scan]` 输出，cnt 增长即通过。向用户汇报。

---

### Task 4: 手柄识别测试

**Files:** 无代码改动（观察 + 记录）。需要用户配合开关手柄。

**Interfaces:**
- Consumes: Task 3 中 running 的 board_b default 扫描器。
- Produces: 手柄地址、RSSI、广播数据记录（更新 `docs/bs21-development.md` 的 M5 清单或记录到日志）。

- [ ] **Step 1: 请用户开机手柄（PC 模式）放在 board_b 旁，持续抓 log**

Run:
```bash
MODB=/dev/serial/by-path/pci-0000:00:14.0-usb-0:3.4.1.3:1.0-port0
stty -F $MODB 115200 raw -echo
cat $MODB > /tmp/opencode/controller_scan.log &
# 等用户开关手柄后 Ctrl-C / kill
```
观察：手柄开机 → `[scan]` 新增一行地址；手柄关机 → 该地址 `cnt` 不再增长。

- [ ] **Step 2: 锁定手柄地址**

确认地址后，记录到 `docs/bs21-development.md`（M5 部分）或单独日志，供下一阶段连接使用。

- [ ] **Step 3: Commit（若更新了文档）**

```bash
git add docs/bs21-development.md
git commit -m "docs: record controller SLE address from scan"
```

---

## Self-Review 备注

- Spec 覆盖：扫描流程（Task 2）、聚合统计（Task 2）、2 秒打印（Task 2）、32 项上限（Task 2 `SCAN_TABLE_SIZE`）、板对板测试（Task 3）、手柄识别（Task 4）、central 配置前置（Task 1，spec 未写但实现必需）。
- 类型一致性：`scan_device_t`、`seek_result_cb(sle_seek_result_info_t *)`、`scan_task(const char *)` 全计划一致。
- 无占位符：所有代码/命令完整给出。