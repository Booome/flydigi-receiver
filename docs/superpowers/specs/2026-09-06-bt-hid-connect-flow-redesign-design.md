# BT HID 连接流程整体重设计 spec

> **范围**：本文是 **`apps/default/` 连接流程整体重设计**（不再打补丁）的 spec。审计与未覆盖场景清单见 `docs/testing/apps-default-connect-audit.md`（§二/§三/§六）。实施依据 = 本 spec。
>
> **基线**：commit `93a1bfd`（审计文档提交点）。所有"现有行为"以此为参照。

## 一、背景与目标

### 1.1 死锁收敛点（审计 §四）

所有 ✗ 行收敛到同一状态组合：
```
模块 NVS 有 link key (b5:...:75)
  + 模块 Bluedroid controller volatile key table = none（controller 刚 init 没装回 key）
  + 手柄有 link key（板子 bda）
  + 手柄 bonded_reconnect_pageonly（page 我们）
```
触发路径：模块断电 / 重启 / controller disable+enable / 部分 NVS 擦 / 连接运行中模块断电 / 手柄+模块同时断电（模块先起）/ connected 时反复拔插模块。

### 1.2 Bluedroid v6.0.2 公开 API 三大限制（不可破）

1. **没有公开 API** 把 NVS link key 灌回 controller volatile table（模块断电后 key 永远在 NVS，controller 永远不知道）。
2. **没有公开 API** 强制 controller 在入站 ACL 上"忽略对端旧 key、触发全新 SSP"（手柄在 bonded_reconnect 模式下收到空 key 的 dev_open 直接拒，实测 AUTH_CMPL stat=10 FAIL）。
3. **没有公开 API** 检测入站 ACL 后让对端"放弃 bonded_reconnect、进入 discoverable"（手柄厂商行为，未实测）。

### 1.3 硬约束（项目负责人确认）

> **任意情况下（不论 §二 24 个场景覆盖与否），用户长按手柄配对键做一次 re-pair，模块必须在合理时间内重新连上。**

推论：
- 模块**任何**状态（除 CONNECTED）都必须保持 inquiry 探测（不能因内部状态错误停止 inquiry）。
- **#3 re-pair 路径必须永远工作**——这是兜底契约，不允许被任何后续 fix 退化。
- 即便死锁收敛点无法自动恢复，**只要用户 re-pair 一次就能解**。

### 1.4 设计目标

| 目标 | 说明 |
|---|---|
| **G1** 修死锁收敛点 | 检测 + 自动恢复（依赖手柄厂商行为）+ 最差用户操作兜底 |
| **G2** 保证硬约束 | 任意状态 inquiry 探测保持；#3 re-pair 路径不退化 |
| **G3** 保持现有 working 路径不退化 | #1 #2 #3 #5 #11 #17 #18 实测通过的路径不能被新设计打断 |
| **G4** 不引入侵入性 API | 不走内部符号 / vendor HCI（避开 §1.2 限制 1 路线） |
| **G5** 不假装能修 | 检测到不能自动恢复时，**清晰打印**让用户做"长按手柄配对键"操作 |

## 二、当前架构能力（baseline @ `93a1bfd`）

详见审计 §三。关键：
- 9 条路径 ✗ 全部收敛到死锁收敛点。
- 当前 ACL_CONN 处理（`e242cfa`）实测**无效**（手柄拒空 key dev_open，AUTH FAIL stat=10）。
- `cb68343` 已修"裸 timeout 清键"逻辑（防止**新**死锁产生）。

## 三、重设计

### 3.1 设计总览

**核心思路**：**接受"模块 NVS 有 key + controller 无 key"的不对称不可自动消除**（Bluedroid v6.0.2 限制 1+2），把恢复责任**部分外包给手柄**：清掉我们的 NVS bond 让状态干净，**保持 page-scan ON** 让手柄继续 page 我们但用空 key 失败，**期待手柄在 ≤90s 内**（vendor 行为，**未实测**，典型 Bluetooth spec 建议行为）**主动放弃 bonded_reconnect 进入 discoverable**，我们 inquiry 命中 → 全新 SSP → 恢复。同时**严格保证硬约束**：任何状态下 inquiry 探测保持，re-pair 信号必响应。

**为什么不直接 dev_open(bda) 重试**：实测 `e242cfa` 已证手柄在 bonded_reconnect 下拒绝空 key dev_open（AUTH FAIL stat=10）。**主动 dev_open 在此状态下无效**，所以放弃这条路径。

**为什么不用 NVS 灌回 controller**：Bluedroid v6.0.2 无公开 API（限制 1），侵入性大（G4）。

**为什么禁用 page-scan（C 方案）不是默认**：禁用 page-scan 会让手柄**完全**连不上我们（包括手柄放弃 page-only 切 discoverable 后的 discoverable 阶段），可能延迟恢复；**保持 page-scan ON** + 持续 inquiry 是更稳的兜底（手柄放弃后我们 inquiry 立即命中）。

### 3.2 状态机（新增 / 修改）

**新增全局状态**：
```c
static bool g_deadlock_recovery;       // 当前是否在死锁恢复模式
static uint8_t g_deadlock_bda[6];     // 触发死锁的对端 BDA（用于日志，不用于去重）
static esp_timer_handle_t g_deadlock_timer; // one-shot，触发后放弃自动恢复 + 打印用户操作提示
```

**状态变量语义**：
- `g_deadlock_recovery == true`：进入死锁恢复模式。
  - 持续 inquiry 探测（**永远保持**，即便在死锁模式）。
  - scan_mode 保持 `ESP_BT_CONNECTABLE | ESP_BT_NON_DISCOVERABLE`（**保持 CONNECTABLE** 让手柄继续 page 我们，加速手柄放弃；保持 NON_DISCOVERABLE 让自己不出现在别人的 inquiry 里）。
  - 忽略 `ACL_CONN_CMPL_STAT_EVT` 的进一步触发（避免重入）。
- `g_deadlock_recovery == false`：正常逻辑（现有行为）。

### 3.3 触发条件

**`ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT` 事件**（已有，v6.0.2 公开 API，带 BDA）：

```
if (g_state == ST_CONNECTED) return;          // 已有连接，忽略
if (g_deadlock_recovery) return;             // 已在死锁恢复，忽略
const uint8_t *bda = param->acl_conn_cmpl_stat.bda;
int nb = esp_bt_gap_get_bond_device_num();
if (nb == 0) {
    // 进入死锁恢复模式（模块 NVS 没 key + 收到入站 ACL = 不对称）
    g_deadlock_recovery = true;
    memcpy(g_deadlock_bda, bda, 6);
    // 清 NVS bond（如果存在则清掉，防止重入；无 key 也安全 idempotent）
    esp_bt_gap_remove_bond_device(peer);   // peer 是 bda 的非 const 拷贝
    // 确保 scan_mode 是 CONNECTABLE + NON_DISCOVERABLE（手柄 page 我们但我们不出现在别人 inquiry 里）
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    // 启动超时定时器（详见 3.4）
    esp_timer_start_once(g_deadlock_timer, DEADLOCK_RECOVER_TIMEOUT_US);
    // 打印诊断信息
    printf("[gap] DEADLOCK detected: inbound ACL from pad, no link key on board; clearing NVS bond and waiting for pad to fall back to discoverable\n");
}
else {
    // bonds > 0：正常路径（现有 OPEN OK handler 会处理 inbound page）
    // 不干预
}
```

**关键点**：
- **不再调用 `esp_hidh_dev_open(bda, BT, 0)`**（实测无效）。
- **清 NVS bond**：防止死锁收敛点再次出现；idempotent（无 key 时是 no-op）。
- **不重置 g_state / 不 cancel inquiry**：保持当前状态（如果是 SCANNING 就继续 SCANNING）。
- **打印清晰**：让用户/开发者知道发生了什么。

### 3.4 超时处理

**`DEADLOCK_RECOVER_TIMEOUT`** = 90 秒（可配置，先 hardcode 90s）。

```
// one-shot 定时器 callback
static void deadlock_timer_cb(void *arg) {
    lock();
    if (!g_deadlock_recovery || g_state == ST_CONNECTED) {
        unlock();
        return;
    }
    g_deadlock_recovery = false;
    unlock();
    // 打印用户操作提示
    printf("[gap] DEADLOCK not auto-recovered in 90s. Pad may still be trying bonded-reconnect.\n");
    printf("[gap] Please long-press the pairing button on the pad to force fresh re-pair.\n");
}
```

**为什么 90s**：
- 给手柄足够时间放弃 page-only（vendor 行为，未实测；90s 是合理上界）。
- 超过 90s 没恢复几乎可以确定手柄不会自动放弃 → 引导用户操作。

### 3.5 恢复（inquiry 命中 → OPEN OK）

如果死锁期间手柄**真的**放弃 page-only 并进入 discoverable → 我们的 inquiry 在跑 → 命中候选 → 走现有 `candidate_update` → `lock_tick` 触发 `open_candidate` → `issue_connect` → `esp_hidh_dev_open` → SSP Just Works → AUTH_CMPL OK → OPEN OK → reset g_deadlock_recovery + disarm timer。

**修改 `OPEN OK` handler**（已有）：
```
case ESP_HIDH_OPEN_EVENT:
    if (param->open.status == ESP_OK) {
        lock();
        g_deadlock_recovery = false;   // 新增：清除死锁模式
        esp_timer_stop(g_deadlock_timer); // 新增：取消超时定时器
        ...
    }
```

**修改 `OPEN FAIL` / `connect_timeout` handler**：**不清 g_deadlock_recovery**（继续保持死锁模式，让手柄继续 page 失败直到放弃）。

**修改 `CLOSE` handler**：同样**不清 g_deadlock_recovery**（避免在恢复过程中误清）。

### 3.6 硬约束保证

| 推论 | 实现 |
|---|---|
| 模块任何状态（除 CONNECTED）都必须保持 inquiry 探测 | `lock_tick` 持续评估候选（已有）；死锁模式下不 cancel inquiry（修改点 3.3） |
| #3 re-pair 路径永远工作 | 不修改 `begin_scan_round` / `candidate_update` / `lock_tick_cb` 的核心逻辑；只增加死锁模式的旁路状态 |
| 死锁模式不会卡死 | `DEADLOCK_RECOVER_TIMEOUT`（90s）+ 定时器触发后清 `g_deadlock_recovery`，恢复正常流程 |
| 死锁模式不会与现有状态机冲突 | `g_deadlock_recovery` 是**旁路**标记，不替换 `g_state`；现有 `lock_tick`、`resume_scan` 不感知它（除 ACL_CONN handler 检查重入） |

### 3.7 边界情况

| 情况 | 处理 |
|---|---|
| 死锁触发时 `g_state == ST_SCANNING`（最常见，模块在 inquiry） | 保持 SCANNING，继续 inquiry；timer 到期或 OPEN OK 时清死锁标记 |
| 死锁触发时 `g_state == ST_CONNECTING`（极少） | 同上，timeout/OPEN 事件正常处理；OPEN OK 清死锁标记 |
| 死锁触发时 `g_state == ST_CONNECTED` | 已经在 case 顶部 `return`，不进入死锁模式 |
| 死锁期间又有新的 ACL_CONN 到达（重入） | `g_deadlock_recovery` 已 true → case 顶部 return，不重入 |
| 死锁期间 timer 到期（90s 没恢复） | `deadlock_timer_cb` 打印用户提示 + 清 `g_deadlock_recovery`；后续正常路径运行；硬约束保证 #3 re-pair 信号仍被响应 |
| 死锁期间 OPEN OK 到达（手柄放弃后成功） | OPEN OK handler 清 `g_deadlock_recovery` + stop timer；继续正常 |
| 死锁期间 OPEN FAIL（dev_open 失败） | **不**清 `g_deadlock_recovery`（避免在恢复中误清）；继续等 timer 或下次 ACL_CONN |

### 3.8 修改点总览

| 位置 | 修改 |
|---|---|
| `bt_stack.c` | `bt_stack_start` 末尾仍设 `ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE`（与现有逻辑一致；CONNECTABLE 一直保持是硬约束的核心） |
| `main.c` 新增常量 | `DEADLOCK_RECOVER_TIMEOUT_MS = 90000` |
| `main.c` 新增全局 | `g_deadlock_recovery`、`g_deadlock_bda`、`g_deadlock_timer` |
| `main.c` `app_main` | 新增 `g_deadlock_timer` 创建（one-shot） |
| `main.c` `bt_gap_cb` ACL_CONN_CMPL 分支 | 改成进入死锁恢复模式（不再调 dev_open）；清 NVS；启动 timer |
| `main.c` 新增 `deadlock_timer_cb` | 超时打印用户提示 + 清 `g_deadlock_recovery` |
| `main.c` OPEN OK 分支 | 清 `g_deadlock_recovery` + stop timer |
| `main.c` `reset_scan_state` | 清 `g_deadlock_recovery`（在状态完全重置时也清掉） |

**未修改**：
- `lock_tick_cb`、`resume_scan`、`begin_scan_round`、`probe_then_page_or_pair`、`open_candidate`、`open_bonded`、`issue_connect`、`handle_disc_result`、`bt_gap_cb` 的 DISC_STATE_CHANGED、SSP、AUTH_CMPL、KEY_REQ 等其他分支。
- 所有现有 fix 行为（probe 启发式 `241277d`、g_probe 守卫 `3499bfe`/`ee05752`、不裸 timeout 清键 `cb68343`）。

## 四、组件与数据流

```
[手柄 bonded_reconnect_pageonly]
        │ page 我们
        ▼
[BT controller 接受 ACL, encryption 失败（无 key），ACL 仍 UP]
        │ ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT (BDA)
        ▼
[bt_gap_cb] ACL_CONN 分支
        │
        ├─ g_state==CONNECTED → return
        ├─ g_deadlock_recovery → return
        ├─ bonds > 0 → 不干预（让 OPEN OK handler 处理）
        └─ bonds == 0:
            g_deadlock_recovery = true
            清 NVS bond (idempotent)
            set_scan_mode(CONNECTABLE | NON_DISCOVERABLE)
            start_one_shot_timer(90s)
            log diagnostic
        │
        ▼ (持续 inquiry 在跑, lock_tick 评估候选)
        │
[手柄 page 我们失败 N 次 → 放弃 page-only → 进 pairing_discoverable]
        │ inquiry 结果 (DISC_RES)
        ▼
[handle_disc_result → candidate_update]
        │
        ▼ (lock_tick 触发 open_candidate)
        │
[issue_connect → esp_hidh_dev_open → SSP Just Works → AUTH_CMPL OK]
        │
        ▼
[OPEN OK handler]
        │
        ├─ halt_scanning_side_effects
        ├─ g_deadlock_recovery = false (新)
        ├─ esp_timer_stop(g_deadlock_timer) (新)
        └─ g_state = ST_CONNECTED
        │
        ▼
[CONNECTED, 收 INPUT reports]

─────────── 或 ───────────

[deadlock_timer 在 +90s 触发]
        │
        ▼
[deadlock_timer_cb]
        │
        ├─ g_deadlock_recovery = false (释放)
        └─ 打印用户操作提示
        │
        ▼ (硬约束: inquiry 仍在跑, scan_mode 仍 CONNECTABLE)
        │
[用户长按手柄配对键 → 手柄进 pairing_discoverable]
        │
        ▼
[inquiry 命中 → 全新 SSP → OPEN OK → CONNECTED]
```

## 五、错误处理

| 错误 | 处理 |
|---|---|
| `esp_bt_gap_remove_bond_device` 返回失败（无 key 时 no-op，但保险） | 打印警告，继续 |
| `esp_bt_gap_set_scan_mode` 返回失败 | 打印警告，继续（scan_mode 在 connect 时也会被重设） |
| `esp_timer_start_once` 失败 | 打印警告，**不**进入死锁模式（降级为原有行为） |
| `deadlock_timer_cb` 触发时 `g_state == ST_CONNECTED` | 直接 return（已经有连接，不需要操作） |
| `deadlock_timer_cb` 触发时 `g_deadlock_recovery == false` | 直接 return（已被 OPEN OK 清掉） |

## 六、测试 / 验证

### 6.1 不退化测试（必须全过）

- MTM 场景 1（fresh pair）：~3s 内连上
- MTM 场景 2（手柄断电→上电 bonded reconnect）：~2-4s 内连上
- MTM 场景 3（re-pair 一致性）：3-5 次都 ~1.5-2.5s 连上
- MTM 场景 4（power-off/power-on，bond 保持）：~2-4s 内连上
- MTM 场景 5（stale-key auto-recover）：OPEN FAIL → 全新 SSP ~3-6s

### 6.2 修死锁测试（必须过）

- **新场景 #4 + #6**：模块断电 → 上电（手柄不动，bonds 已配过）
  - 启动后 `[gap] DEADLOCK detected ...` 打印
  - NVS bond 清掉
  - 持续 inquiry 探测（**硬约束保证**）
  - 等手柄放弃 page-only（vendor 行为，**需实测确认时延**）
  - 若手柄在 90s 内放弃：inquiry 命中 → 全新 SSP → 连上
  - 若手柄未在 90s 内放弃：打印 `[gap] DEADLOCK not auto-recovered in 90s. Please long-press the pairing button on the pad to force fresh re-pair.`
  - **关键**：无论自动恢复成功否，用户做一次 re-pair **必须**能连上（硬约束）

### 6.3 全审计矩阵回归（24 条）

按 `apps-default-connect-audit.md` §三 矩阵跑一遍（结合 MTM + 新增场景）。重点：
- #4 / #6 / #19（模块断电/拔插）
- #11/#12/#13（运行中断电）
- #23/#24（部分擦 / controller 重置）

### 6.4 测试记录

- spec §十一 追加实测数据（时延 / log 片段）
- 更新 MTM（`apps-default-manual-tests.md`）场景 7 状态：原"已知未修" → "已修（依赖手柄厂商行为，兜底靠 re-pair）"

## 七、范围外（不做）

- **切连接模式（#21）**：手柄切换 X-input ↔ Switch Pro → 白名单过滤失败 → 用户需重新配对。记为后续 Task，不在本里程碑。
- **NVS 部分擦恢复（#23）**：仍需 re-pair 兜底（硬约束），不专门处理。
- **完全无键 NVS 第一次配对**：fresh pair 路径（已有），不走死锁模式。

## 八、风险

| 风险 | 缓解 |
|---|---|
| 手柄在 page-only 失败后**不**主动放弃（vendor bug）→ 90s 仍死锁 | 90s 后打印用户提示（兜底硬约束 re-pair） |
| 死锁模式 + 用户长时间不操作 → 模块持续 inquiry 占用 RF | RF 占用 vs 板子可用性，可接受（无键时本就要 inquiry） |
| `remove_bond_of` 失败 → 死锁状态持续 | 打印警告但流程不变（下次启动仍能恢复） |
| 死锁期间 timer 启动失败 → 无超时保护 | 降级为原 `e242cfa` 行为（不假装恢复） |
| 多对端场景（未来可能） | 当前单 bda 设计；多对端需扩展（记为未来） |

## 九、依赖

- v6.0.2 Bluedroid 公开 API：`ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT`、`esp_bt_gap_get_bond_device_num`、`esp_bt_gap_remove_bond_device`、`esp_bt_gap_set_scan_mode`、`esp_timer_start_once`、`esp_hidh_dev_open`、`ESP_HIDH_OPEN_EVENT`（均已有）。
- 不引入新依赖。

## 十、完成定义

- §六.1 不退化测试全过。
- §六.2 修死锁测试：要么 90s 内自动恢复，要么打印清晰用户操作提示；用户 re-pair 后必连上。
- §六.3 24 条审计矩阵全过。
- 代码风格：clang-format 干净；无 `(void)arg`；不破坏既有 fix。
- AGENTS.md / README.md 同步。

## 十一、验证记录

（实施后填）
