# M6: SLE 连接尝试 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 `default` app 从纯扫描器改造为"扫描 → 连接 → 配对"全自动状态机，用锁定的手柄地址 `a1:a2:c8:75:43:b8` 发起 SLE 连接并观察各阶段状态。

**Architecture:** 显式四状态机（SCAN → CONNECTING → PAIRING → ACTIVE），全部转换由 SDK 回调驱动；回调路径零阻塞。连接参数用 SDK 默认；断开/失败无限回扫重试。

**Tech Stack:** BS21 (Hi2821) Ai-BS21_SDK，SLE central（M5 已启用），`sle_connection_manager.h` API。

## Global Constraints

- SDK（`~/.local/Ai-BS21_SDK`）只读引用，绝不修改 SDK 源码
- 只允许修改 `wireless/bs21/apps/default/main.c` 和 `wireless/bs21/apps/t_broadcaster/main.c`
- 代码注释用英文；不引入中文
- 回调路径（`seek_result_cb` / `seek_disable_cb` / `connect_state_changed_cb` / `pair_complete_cb` 及其调用路径）只做内存状态修改 + `osal_printk` 日志，禁止阻塞、禁止长时间等待
- 连接参数全部用 SDK 默认，不调 `sle_default_connection_param_set`
- 手柄会自动关机（省电）：测试中若扫描不到手柄（log 无 `[conn] target locked`），必须停下提醒用户重新开手柄
- 构建：default 在顶层 build 目录（`cmake -S wireless/bs21 -B wireless/bs21/build`），t_broadcaster 在 `build/t_broadcaster`（`-DBS21_APP=t_broadcaster`）

---

### Task 1: main.c 状态机改造

**Files:**
- Modify: `wireless/bs21/apps/default/main.c`（整体改造）

**Interfaces:**
- Consumes: M5 已启用的 SLE central 配置（`SUPPORT_SLE_CENTRAL`）、现有扫描器代码（`scan_task` / `print_scan_table` / `table_find` / `table_add` / `bs21_rst` / `sle_power_on_cb` / `sle_enable_cb` / `scan_start` / `seek_enable_cb`）
- Produces: 状态枚举 `conn_state_t`、目标地址宏 `CONNECT_TARGET_ADDR`、`g_target_addr`（运行时填充的完整 `sle_addr_t`，含 type）、回调 `seek_disable_cb` / `conn_state_changed_cb` / `pair_complete_cb`、`conn_rescan()`

- [ ] **Step 1: 整体替换 `apps/default/main.c` 为以下内容**

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
#include "sle_connection_manager.h"

#define SCAN_TABLE_SIZE  32
#define SCAN_PRINT_MS    2000

#define CONNECT_TARGET_ADDR  { 0xa1, 0xa2, 0xc8, 0x75, 0x43, 0xb8 }

typedef enum {
    CONN_STATE_SCAN = 0,
    CONN_STATE_CONNECTING,
    CONN_STATE_PAIRING,
    CONN_STATE_ACTIVE,
} conn_state_t;

typedef struct {
    sle_addr_t addr;
    int8_t rssi;
    uint32_t count;
    uint8_t used;
} scan_device_t;

static scan_device_t g_scan_table[SCAN_TABLE_SIZE] = { 0 };
static bool g_table_full = false;
static sle_dev_manager_callbacks_t g_dev_cbk = { 0 };
static sle_announce_seek_callbacks_t g_seek_cbk = { 0 };
static sle_connection_callbacks_t g_conn_cbk = { 0 };

static conn_state_t g_conn_state = CONN_STATE_SCAN;
static bool g_target_locked = false;
static sle_addr_t g_target_addr = { 0 };
static const uint8_t g_target_mac[SLE_ADDR_LEN] = CONNECT_TARGET_ADDR;

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
    g_table_full = true;
    return NULL;
}

static void conn_rescan(void)
{
    g_target_locked = false;
    g_conn_state = CONN_STATE_SCAN;
    osal_printk("[conn] rescan\r\n");
    (void)sle_start_seek();
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
    if (!g_target_locked &&
        memcmp(result->addr.addr, g_target_mac, SLE_ADDR_LEN) == 0) {
        (void)memcpy_s(&g_target_addr, sizeof(g_target_addr),
                       &result->addr, sizeof(g_target_addr));
        g_target_locked = true;
        osal_printk("[conn] target locked, stopping seek\r\n");
        (void)sle_stop_seek();
    }
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
    if (g_table_full) {
        osal_printk("[scan] table full\r\n");
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

static void conn_state_changed_cb(uint16_t conn_id, const sle_addr_t *addr,
                                  sle_acb_state_t conn_state, sle_pair_state_t pair_state,
                                  sle_disc_reason_t disc_reason)
{
    osal_printk("[conn] conn id:%u state:%d pair:%d disc:0x%x\r\n",
                conn_id, conn_state, pair_state, disc_reason);
    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        if (pair_state == SLE_PAIR_NONE) {
            g_conn_state = CONN_STATE_PAIRING;
            osal_printk("[conn] pairing...\r\n");
            if (sle_pair_remote_device(addr) != ERRCODE_SUCC) {
                osal_printk("[conn] pair request fail\r\n");
            }
        } else {
            g_conn_state = CONN_STATE_ACTIVE;
        }
    } else {
        conn_rescan();
    }
}

static void pair_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    osal_printk("[conn] paired: 0x%x\r\n", status);
    if (status == ERRCODE_SUCC) {
        g_conn_state = CONN_STATE_ACTIVE;
    }
}

static void seek_disable_cb(errcode_t status)
{
    osal_printk("[conn] seek disabled: 0x%x\r\n", status);
    if (status != ERRCODE_SUCC) {
        osal_msleep(100);
        conn_rescan();
        return;
    }
    g_conn_state = CONN_STATE_CONNECTING;
    osal_printk("[conn] connecting...\r\n");
    if (sle_connect_remote_device(&g_target_addr) != ERRCODE_SUCC) {
        osal_printk("[conn] connect fail\r\n");
        conn_rescan();
    }
}

static void scan_start(void);

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
    g_seek_cbk.seek_disable_cb = seek_disable_cb;

    g_conn_cbk.connect_state_changed_cb = conn_state_changed_cb;
    g_conn_cbk.pair_complete_cb = pair_complete_cb;
    sle_connection_register_callbacks(&g_conn_cbk);

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

- [ ] **Step 2: 编译验证**

Run: `cmake -S wireless/bs21 -B wireless/bs21/build && cmake --build wireless/bs21/build -j`
Expected: `[100%] Built target package`，无 error（若报 `sle_connect_remote_device` 等未定义，说明 central 配置失效，回查 Task 1 的 gen-config 提交）

- [ ] **Step 3: fwpkg patch 表回归**

Run:
```bash
python3 - <<'EOF'
d = open('wireless/bs21/build/bs21_all_in_one.fwpkg','rb').read()
print("TBL:", bytes.fromhex('efa28239') in d)
print("CMP:", bytes.fromhex('000000000000040024000000') in d)
EOF
```
Expected: `TBL: True` 且 `CMP: True`

- [ ] **Step 4: Commit**

```bash
git add wireless/bs21/apps/default/main.c
git commit -m "feat: add SLE connection state machine to default app"
```

---

### Task 2: t_broadcaster 地址非零化 + 板对板连接验证

**Files:**
- Modify: `wireless/bs21/apps/t_broadcaster/main.c`（`set_announce_param()` 内 `local_addr`）
- Modify（本地临时，不 commit）: `wireless/bs21/apps/default/main.c` 的 `CONNECT_TARGET_ADDR`

**Interfaces:**
- Consumes: Task 1 的状态机代码
- Produces: board_a 以非零地址广播；板对板连接流程验证结果

- [ ] **Step 1: t_broadcaster 广播地址非零化**

在 `wireless/bs21/apps/t_broadcaster/main.c` 的 `set_announce_param()` 中，把
`unsigned char local_addr[SLE_ADDR_LEN] = { 0 };` 改为
`unsigned char local_addr[SLE_ADDR_LEN] = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x01 };`
（全零地址不能作为连接目标；`param.announce_mode` 已是 `SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE` 可被连接）

- [ ] **Step 2: 构建 t_broadcaster**

Run: `cmake -S wireless/bs21 -B wireless/bs21/build/t_broadcaster -DBS21_APP=t_broadcaster && cmake --build wireless/bs21/build/t_broadcaster -j`
Expected: `Built target package` 无 error

- [ ] **Step 3: 板对板测试（本地临时改 default 目标地址，不 commit）**

在 `wireless/bs21/apps/default/main.c` 把 `#define CONNECT_TARGET_ADDR  { 0xa1, 0xa2, 0xc8, 0x75, 0x43, 0xb8 }`
改为 `#define CONNECT_TARGET_ADDR  { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x01 }`（指向 board_a）。

Run: `cmake --build wireless/bs21/build -j`

- [ ] **Step 4: 烧录**

Run: `python3 wireless/bs21/tools/burn.py board_a -a t_broadcaster` 然后
`python3 wireless/bs21/tools/burn.py board_b -a default`
Expected: 两块板 `flashed successfully`

- [ ] **Step 5: 观察连接流程**

复位 board_a（t_broadcaster 广播），抓 board_b 串口约 15s：
```bash
# 复位 board_a 与 board_b（ctrl_port / reset_pin 见 wireless/bs21/tools/burn_config.yaml）
# board_b 串口抓 log
stty -F <board_b_serial> 115200 raw -echo
cat <board_b_serial> > /tmp/m6_b2b.log &
sleep 15
kill %1
grep -aE "\[conn\]|\[scan\]" /tmp/m6_b2b.log
```
Expected（连接成功路径）：`[conn] target locked` → `[conn] seek disabled: 0x0` →
`[conn] connecting...` → `[conn] conn id:N state:1 pair:.. disc:0x0`（CONNECTED=1）
→ `[conn] pairing...` / `[conn] paired: 0x...`。
若为失败路径（state:0 NONE 或 state:2 DISCONNECTED）→ `[conn] rescan` → 无限重试循环，
也视为流程正确（验证了重试机制）。

- [ ] **Step 6: 恢复 default 目标地址并 commit**

Run: `git checkout -- wireless/bs21/apps/default/main.c`（把 Task 1 的提交版本还原，目标地址回到手柄）
```
git add wireless/bs21/apps/t_broadcaster/main.c
git commit -m "fix: use non-zero announce addr in t_broadcaster"
```

---

### Task 3: 手柄连接验证

**Files:**
- 无代码改动（手柄地址已是 Task 1 提交的默认值）
- Modify: `docs/bs21-development.md`（记录结果）

**Interfaces:**
- Consumes: Task 1 状态机（目标地址 = 手柄 `a1:a2:c8:75:43:b8`）
- Produces: 手柄连接/配对/断链各阶段观察记录

- [ ] **Step 1: 编译烧录**

Run: `cmake --build wireless/bs21/build -j` 然后
`python3 wireless/bs21/tools/burn.py board_b -a default`
Expected: 烧录成功

- [ ] **Step 2: 用户开手柄（PC 模式）放 board_b 旁**

主动向用户说明：请开手柄并保持开机。手柄会自动关机（省电）——若抓 log 后
长时间没有 `[conn] target locked`，停下提醒用户重新开手柄。

- [ ] **Step 3: 抓取并观察连接日志**

抓 board_b 串口（复位 board_b 触发启动），观察：
- 扫描到手柄：`[scan] devices:1` + `[conn] target locked, stopping seek`
- 连接：`[conn] connecting...` → `[conn] conn id:N state:1`（CONNECTED）
- 配对：`[conn] pairing...` → `[conn] paired: 0x0`（成功）/ 非 0（拒绝原因）
- 断链：`[conn] disconnected`（`disc:0x10` 远端 / `0x11` 本端）→ `[conn] rescan`

若手柄自动关机：`[conn] rescan` 后一直无 `target locked`，停下提醒用户重新开手柄，
再抓一轮。

- [ ] **Step 4: 记录结果到文档**

更新 `docs/bs21-development.md` 的 M6 部分：
- 勾选已完成的路线图项（连接发起、连接状态观察、配对尝试）
- 在"识别结果"块后新增 M6 验证记录：连接是否建立、配对结果、断链原因、
  `disc_reason` 数值含义

- [ ] **Step 5: Commit**

```bash
git add docs/bs21-development.md
git commit -m "docs: record M6 connection verification results"
```