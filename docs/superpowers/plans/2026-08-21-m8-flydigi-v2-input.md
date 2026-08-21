# M8 飞智 V2 协议激活 SLE 输入流 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 Apex 5 手柄经 SLE 流式输出 `5a a5 ef` 32 字节输入报告（逆向并验证飞智 V2 协议）。

**Architecture:** 复用 `apps/sle_probe`。先修复 M7 的通知开启 bug（向正确的 CCC 描述符 handle 写 `0x0001`，而非 property value handle），再 replay 飞智 V2 的 init+enable 已知字节（`5a a5 ...` 帧）到可写 handle，触发控制器进入 extended 模式并流式输出输入报告；全程强 hex 日志 + 魔术前缀检测。

**Tech Stack:** Ai-BS21_SDK（SLE/SSAP，只读引用）、BS21 固件（C）、CMake（`BS21_APP` 多 app 构建）。

## Global Constraints

- SDK 只读引用模式：不修改 `~/.local/Ai-BS21_SDK` 任何源码。
- `-Werror`，所有编译必须 0 warning。
- 不修改 default/其他现有 app 的行为逻辑。
- 所有文件修改在 worktree `m8-flydigi-protocol`（路径 `.worktrees/m8-flydigi-protocol`）进行。
- 代码注释英文，保持简短（只写 why）。
- 每个 task 完成后 run code-simplifier 技能做简化 pass 并保持编译通过。
- 编译命令：`cmake -S wireless/bs21 -B wireless/bs21/build -DBS21_APP=sle_probe && cmake --build wireless/bs21/build -j`
- 烧录命令：`fuser -k /dev/ttyUSB6 2>/dev/null; rm -f /var/lock/LCK..ttyUSB6; python3 wireless/bs21/tools/burn.py board_a`
- 飞智 V2 协议参考：`docs/reference/flydigi/vader5.toml`、`vader5-protocol.md`（init/enable 字节、报告布局）。
- SSAP 约束（来自 SDK `sle_ssap_stru.h`）：`SSAP_FIND_TYPE` 枚举**无 DESCRIPTOR 类型**；property 结果只给 `descriptors_count` + `descriptors_type[]`（0x02=CCC）。写命令 `ssapc_write_param_t.type` 用 `ssap_property_type_t`：`SSAP_PROPERTY_TYPE_VALUE=0x00`、`SSAP_DESCRIPTOR_CLIENT_CONFIGURATION=0x02`。CCC 描述符 handle 不可直接发现，按 GATT 惯例 = `property_handle + 1`（兜底 `+2`）。

---

### Task 1: 修复通知开启（CCC 描述符写）

**Files:**
- Modify: `wireless/bs21/apps/sle_probe/sle_probe_client.c`
- Modify: `wireless/bs21/apps/sle_probe/sle_probe_client.h`

**Interfaces:**
- Consumes: 现有 `g_conn_id`、SDK `ssapc_write_req` / `ssapc_write_param_t`、`SSAP_DESCRIPTOR_CLIENT_CONFIGURATION`。
- Produces: `g_notify_hdls[8]` / `g_notify_cnt` —— 收集 notify 能力 property handle，供本 task 开启通知与 Task 2 发送命令使用。

- [ ] **Step 1: 头文件增加 notify 收集数组声明**

`wireless/bs21/apps/sle_probe/sle_probe_client.h` 末尾追加：

```c
void probe_init(void);
```

`wireless/bs21/apps/sle_probe/sle_probe_client.c` 顶部 static 区（紧邻 `g_write_hdls`）追加：

```c
static uint16_t g_notify_hdls[8];
static uint8_t  g_notify_cnt = 0;
```

- [ ] **Step 2: find_property_cb 收集 notify property**

将现有 `probe_find_property_cb` 中收集可写 handle 的逻辑之后，追加 notify 收集。完整替换为：

```c
static void probe_find_property_cb(uint8_t client_id, uint16_t conn_id,
                                   ssapc_find_property_result_t *property, errcode_t status)
{
    (void)client_id;
    (void)conn_id;
    if (status != ERRCODE_SUCC || property == NULL) {
        osal_printk("%s find property: status=0x%x\r\n", PROBE_LOG, status);
        return;
    }
    osal_printk("%s find property: status=0x%x handle=0x%x oper_ind=%u desc=%u uuid=",
                PROBE_LOG, status, property->handle, property->operate_indication,
                property->descriptors_count);
    for (uint8_t i = 0; i < property->uuid.len && i < 16; i++) {
        osal_printk("%02x ", property->uuid.uuid[i]);
    }
    osal_printk("\r\n");
    if ((property->operate_indication & (SSAP_OPERATE_INDICATION_BIT_WRITE |
                                         SSAP_OPERATE_INDICATION_BIT_WRITE_NO_RSP)) &&
        g_write_cnt < 8) {
        g_write_hdls[g_write_cnt++] = property->handle;
    }
    if ((property->operate_indication & (SSAP_OPERATE_INDICATION_BIT_NOTIFY |
                                         SSAP_OPERATE_INDICATION_BIT_INDICATE)) &&
        g_notify_cnt < 8) {
        g_notify_hdls[g_notify_cnt++] = property->handle;
    }
}
```

注：位宏来自 SDK `sle_ssap_stru.h`：`SSAP_OPERATE_INDICATION_BIT_NOTIFY=0x08`、`SSAP_OPERATE_INDICATION_BIT_INDICATE=0x10`（已确认存在）。

- [ ] **Step 3: find_cmp_cb 改为向 CCC 描述符写 0x0001**

将现有 `probe_find_cmp_cb` 中"向 0x12–0x19 写 0x0001"的循环替换为向每个 notify property 的 CCC 描述符写。完整替换 `probe_find_cmp_cb` 为：

```c
static void probe_find_cmp_cb(uint8_t client_id, uint16_t conn_id,
                              ssapc_find_structure_result_t *result, errcode_t status)
{
    (void)client_id;
    (void)conn_id;
    if (status != ERRCODE_SUCC || result == NULL) {
        osal_printk("%s find complete: status=0x%x\r\n", PROBE_LOG, status);
        return;
    }
    osal_printk("%s find complete: status=0x%x type=%u uuid_len=%u\r\n",
                PROBE_LOG, status, result->type, result->uuid.len);

    /* Enable notifications: write 0x0001 to the CCC descriptor handle.
       SDK gives no descriptor handle, so try property+1 then +2 (GATT layout). */
    uint8_t ccc[2] = { 0x01, 0x00 };
    for (uint8_t i = 0; i < g_notify_cnt; i++) {
        for (uint8_t off = 1; off <= 2; off++) {
            ssapc_write_param_t wp = { 0 };
            wp.handle = g_notify_hdls[i] + off;
            wp.type = SSAP_DESCRIPTOR_CLIENT_CONFIGURATION;
            wp.data_len = sizeof(ccc);
            wp.data = ccc;
            if (ssapc_write_req(0, g_conn_id, &wp) == ERRCODE_SUCC) {
                osal_printk("%s enable notify on ccc 0x%x (prop 0x%x)\r\n",
                            PROBE_LOG, wp.handle, g_notify_hdls[i]);
            }
        }
    }

    /* V2 init+enable sequence is appended by Task 2. */
}
```

- [ ] **Step 4: 编译验证**

Run: `cmake -S wireless/bs21 -B wireless/bs21/build -DBS21_APP=sle_probe && cmake --build wireless/bs21/build -j`
Expected: 0 warning（-Werror）。

- [ ] **Step 5: Commit**

```bash
git add wireless/bs21/apps/sle_probe/sle_probe_client.c wireless/bs21/apps/sle_probe/sle_probe_client.h
git commit -m "fix(sle_probe): enable notifications on CCC descriptor handle"
```

---

### Task 2: 加入 V2 协议试炼序列器

**Files:**
- Create: `wireless/bs21/apps/sle_probe/sle_probe_trials.h`
- Create: `wireless/bs21/apps/sle_probe/sle_probe_trials.c`
- Modify: `wireless/bs21/apps/sle_probe/sle_probe_client.c`（`probe_find_cmp_cb` 末尾调用 `trials_run()`；`main.c` 或 `probe_init` 无需改）

**Interfaces:**
- Consumes: Task 1 的 `g_conn_id`、`g_write_hdls[8]`/`g_write_cnt`（可写 handle 列表）。
- Produces: `void trials_run(void)` —— 向可写 handle 依次发 V2 init×4 + enable×1，每条间延时，每次写前打印。

- [ ] **Step 1: 创建 sle_probe_trials.h**

```c
#ifndef SLE_PROBE_TRIALS_H
#define SLE_PROBE_TRIALS_H

void trials_run(void);

#endif /* SLE_PROBE_TRIALS_H */
```

- [ ] **Step 2: 创建 sle_probe_trials.c（V2 init/enable 字节，来自 vader5.toml）**

```c
#include "soc_osal.h"
#include "securec.h"
#include "sle_ssap_client.h"
#include "sle_probe_client.h"
#include "sle_probe_trials.h"

#define TRIALS_LOG  "[trials]"

/* Flydigi V2 handshake: 5a a5 <cmd> <len> <payload> <chk>.
   init sequence + enable from docs/reference/flydigi/vader5.toml. */
static const uint8_t g_v2_init[][5] = {
    {0x5a, 0xa5, 0x01, 0x02, 0x03},
    {0x5a, 0xa5, 0xa1, 0x02, 0xa3},
    {0x5a, 0xa5, 0x02, 0x02, 0x04},
    {0x5a, 0xa5, 0x04, 0x02, 0x06},
};
static const uint8_t g_v2_enable[11] =
    {0x5a, 0xa5, 0x11, 0x07, 0xff, 0x01, 0xff, 0xff, 0xff, 0x15, 0x00};

static void trials_send(const uint8_t *buf, uint8_t len, uint16_t handle)
{
    ssapc_write_param_t wp = { 0 };
    wp.handle = handle;
    wp.type = SSAP_PROPERTY_TYPE_VALUE;
    wp.data_len = len;
    wp.data = buf;
    osal_printk("%s wrote", TRIALS_LOG);
    for (uint8_t i = 0; i < len; i++) {
        osal_printk(" %02x", buf[i]);
    }
    osal_printk(" to 0x%x\r\n", handle);
    ssapc_write_req(0, g_conn_id, &wp);
}

void trials_run(void)
{
    osal_printk("%s start V2 init+enable\r\n", TRIALS_LOG);
    for (uint8_t w = 0; w < g_write_cnt; w++) {
        uint16_t h = g_write_hdls[w];
        for (uint8_t i = 0; i < sizeof(g_v2_init) / sizeof(g_v2_init[0]); i++) {
            trials_send(g_v2_init[i], sizeof(g_v2_init[i]), h);
            uapi_systick_delay_ms(50);
        }
        trials_send(g_v2_enable, sizeof(g_v2_enable), h);
        uapi_systick_delay_ms(50);
    }
    osal_printk("%s done; watching for 5a a5 ef stream\r\n", TRIALS_LOG);
}
```

注：`uapi_systick_delay_ms(uint32_t)` 已在 SDK `driver/systick.h` 确认存在，直接使用。

- [ ] **Step 3: sle_probe_client.c 接入 trials_run**

在 `sle_probe_client.c` 顶部 include 区追加：

```c
#include "sle_probe_trials.h"
```

在 `probe_find_cmp_cb` 末尾（Task 1 注释 `/* V2 init+enable sequence is appended by Task 2. */` 处）追加调用：

```c
    trials_run();
```

- [ ] **Step 4: 编译验证**

Run: `cmake --build wireless/bs21/build -j`
Expected: 0 warning。

- [ ] **Step 5: Commit**

```bash
git add wireless/bs21/apps/sle_probe
git commit -m "feat(sle_probe): replay Flydigi V2 init+enable sequence"
```

---

### Task 3: 强日志 + 魔术前缀检测 + 报告解析

**Files:**
- Modify: `wireless/bs21/apps/sle_probe/sle_probe_client.c`（`probe_notification_cb` / `probe_indication_cb`）

**Interfaces:**
- Consumes: Task 1/2 触发的通知数据（`ssapc_handle_value_t`）。
- Produces: 控制台打印完整 payload hex；命中 `5a a5 ef` 打印 `*** INPUT STREAM STARTED ***` 并按 Vader5 布局解析轴/按键；命中任意 `5a a5` 前缀打印 `ACK`。

- [ ] **Step 1: 替换通知/指示回调为强日志 + 解析**

将 `probe_notification_cb` 与 `probe_indication_cb` 整体替换为：

```c
static void probe_print_hex(const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        osal_printk("%02x ", buf[i]);
    }
    osal_printk("\r\n");
}

static void probe_parse_report(const uint8_t *d, uint32_t len)
{
    if (len < 32) {
        return;
    }
    int16_t lx = (int16_t)(d[3] | (d[4] << 8));
    int16_t ly = -(int16_t)(d[5] | (d[6] << 8));
    int16_t rx = (int16_t)(d[7] | (d[8] << 8));
    int16_t ry = -(int16_t)(d[9] | (d[10] << 8));
    uint32_t btn = ((uint32_t)d[11]) | ((uint32_t)d[12] << 8) |
                   ((uint32_t)d[13] << 16) | ((uint32_t)d[14] << 24);
    osal_printk("%s AXIS lx=%d ly=%d rx=%d ry=%d lt=%u rt=%u btn=0x%08x\r\n",
                PROBE_LOG, lx, ly, rx, ry, d[15], d[16], btn);
}

static void probe_notification_cb(uint8_t client_id, uint16_t conn_id,
                                  ssapc_handle_value_t *data, errcode_t status)
{
    (void)client_id;
    (void)conn_id;
    (void)status;
    osal_printk("%s recv len=%u ", PROBE_LOG, data->data_len);
    probe_print_hex(data->data, data->data_len);
    if (data->data_len >= 3 && data->data[0] == 0x5a && data->data[1] == 0xa5) {
        if (data->data[2] == 0xef) {
            osal_printk("%s *** INPUT STREAM STARTED *** (5a a5 ef)\r\n", PROBE_LOG);
            probe_parse_report(data->data, data->data_len);
        } else {
            osal_printk("%s ACK (5a a5 %02x)\r\n", PROBE_LOG, data->data[2]);
        }
    }
}

static void probe_indication_cb(uint8_t client_id, uint16_t conn_id,
                                ssapc_handle_value_t *data, errcode_t status)
{
    (void)client_id;
    (void)conn_id;
    (void)status;
    osal_printk("%s ind len=%u ", PROBE_LOG, data->data_len);
    probe_print_hex(data->data, data->data_len);
    if (data->data_len >= 3 && data->data[0] == 0x5a && data->data[1] == 0xa5) {
        if (data->data[2] == 0xef) {
            osal_printk("%s *** INPUT STREAM STARTED *** (5a a5 ef)\r\n", PROBE_LOG);
            probe_parse_report(data->data, data->data_len);
        } else {
            osal_printk("%s ACK (5a a5 %02x)\r\n", PROBE_LOG, data->data[2]);
        }
    }
}
```

（`probe_print_hex` 在 Task 1/2 前已定义于原文件；若已存在则不要重复定义。）

- [ ] **Step 2: 编译验证**

Run: `cmake --build wireless/bs21/build -j`
Expected: 0 warning。

- [ ] **Step 3: Commit**

```bash
git add wireless/bs21/apps/sle_probe/sle_probe_client.c
git commit -m "feat(sle_probe): strong hex logging and 5a a5 ef stream detection"
```

---

### Task 4: 板上验证

**Files:** 无代码改动（记录结果）。

- [ ] **Step 1: 编译 sle_probe 最新**

Run: `cmake -S wireless/bs21 -B wireless/bs21/build -DBS21_APP=sle_probe && cmake --build wireless/bs21/build -j`
Expected: 0 warning。

- [ ] **Step 2: 烧录 board_a**

Run: `fuser -k /dev/ttyUSB6 2>/dev/null; rm -f /var/lock/LCK..ttyUSB6; python3 wireless/bs21/tools/burn.py board_a`
Expected: 烧录成功，设备进入 `app: sle-probe`。

- [ ] **Step 3: 抓 log 并让手柄靠近 + 操作**

```bash
stty -F /dev/ttyUSB6 115200 raw -echo
cat /dev/ttyUSB6 > /tmp/m8.log &
```

- 手柄进 2.4G 模式开机，靠近 board_a。
- 观察：是否出现 `enable notify on ccc 0xNN`、`trials wrote 5a a5 ...`、`ACK (5a a5 ..)`、最终 `*** INPUT STREAM STARTED ***` + 持续 `AXIS lx=.. ly=..` 帧。
- 移动摇杆/按按键，确认轴值与 btn 随动作变化。
- `kill %1` 停止抓 log。

- [ ] **Step 4: 判定与记录**

- 若看到 `*** INPUT STREAM STARTED ***` 且轴/按键随动作变化 → M8 成功，记录触发命令与**实际报告布局**（与 Vader5 假设的偏移差异）。
- 若只有 `ACK` 无 `5a a5 ef` → enable 字节可能 Apex5 变体不同；回到 Task 2 调整 `g_v2_enable` payload（保留 cmdId 0x11），重烧重试。
- 若连 `ACK` 都无 → 检查 Task 1 的 CCC 写是否 `write cfm status=0x0`（确认通知已开）；若通知未开，调整 CCC handle 偏移（+1/+2 之外试 +0 或其它）。

- [ ] **Step 5: Commit 验证记录**

```bash
git add -A
git commit -m "docs: record M8 on-board V2 protocol verification"
```

---

### Task 5: 文档同步

**Files:**
- Modify: `docs/superpowers/plans/2026-08-21-m8-flydigi-v2-input.md`（追加验证结果章节，或新建 `...-results.md`）
- Modify: `AGENTS.md`（补充飞智 V2 协议要点：init/enable 字节、报告头 `5a a5 ef`、SSAP 通知开启约束）

**Interfaces:**
- Consumes: Task 4 的实测结果。

- [ ] **Step 1: 把 Task 4 实测结论写入计划文档**

在 `docs/superpowers/plans/2026-08-21-m8-flydigi-v2-input.md` 末尾追加"## 验证结果"章节：实际触发命令、Apex5 报告布局（与 Vader5 差异）、CCC 偏移最终取值。

- [ ] **Step 2: 更新 AGENTS.md**

在"手柄硬件信息"附近补充：飞智 V2 协议（init 握手 + `5a a5 11 07 ff 01 ff ff ff 15 00` enable；输入报告头 `5a a5 ef` 32B）；SLE/SSAP 开启通知须写 CCC 描述符 handle（property+1/+2），且 SDK 无 descriptor 发现 API。

- [ ] **Step 3: Commit**

```bash
git add docs/
git commit -m "docs: finalize M8 V2 protocol findings and update AGENTS"
```

---

## 验证总览

- 编译：每个 task 编译 0 warning。
- 功能：板上收到 `*** INPUT STREAM STARTED ***` 且 `AXIS` 帧随手柄动作变化 → M8 达成。
- 失败回退：Task 4 的判定分支（调 enable payload / CCC 偏移）。
