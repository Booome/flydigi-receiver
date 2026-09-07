# BT HID 场景 2：bonded 重连 + inquiry-driven fresh — 设计 spec

> **范围**：手柄断电再上电（board 不重启）能自动重连；手柄随时长按配对键 re-pair 都能成功；
> 全程**不需要**手动 `erase_flash` 复位模块。
>
> **基线**：PR #1 merge 后 `main HEAD d9c67e7`。
>
> **状态**：已实机验证（2026-09-07 复现 bug → fix；2026-09-08 死代码清理后 18 轮
> pair/re-pair/reconnect 回归 0.5~2.6s 全过；half-open 边界挂账，见 §5.1）。

## 一、问题链（实测逐层暴露）

1. **PR #1 无任何重连路径**：inbound ACL 只 log；bonded 手柄不可被发现 → inquiry 永不命中。
2. **配对窗口丢失**：candidate 命中后还要等 stable 3s / total 8s，手柄退出配对态后连不上。
3. **auto-reconnect 被 app 抢跑打断**：inbound 后 app 立刻 `dev_open`，与栈自己的 SDP 程序
   竞争（ESP32 一次只跑一个 SDP，esp-idf#10504）→ `ERR_SDP` 风暴。
4. **stale-key 出站被 PAGE_TIMEOUT**：手柄长按配对键后进入 **inquiry-scan**
   模式（可被发现 + 拒绝 page），但 board 不知道，按候选算法 EWMA 锁 3s + 用旧 key 发起 outbound
   `dev_open` → L2CAP `ConnectCfm_Cb Status:4`（HCI `PAGE_TIMEOUT` 0x04）。连续多次后
   `remove_bond_device` 把本端也清了 → 进入"半键"死锁：手柄还在配对模式等 inbound SSP，
   host 没了 key 永远等不到 `AUTH_CMPL`，手柄 UI 卡"连接中"。
5. **第三次强制配对卡"连接中"（本 spec 核心之二）**：连续多次成功 re-pair 后，本端 NVS 有
   有效 bond、手柄清 key 再进配对模式 → outbound auth 带旧 key 被手柄 **0x05 auth-reject**
   （`ConnectCfm Status:5`）。手柄 auth-reject 后关闭全部 scan，UI 永久卡"连接中"，主机不可
   触达。且旧 FAIL 处理里的 `remove_bond` 发生在 HID callback（BT task）内，deferred delete
   永远等不到复验 → key 留在 RAM → 后续每次 outbound 都带 stale key（见 §3.4）。

## 二、参考项目证据链

| 项目 | 结论 |
|---|---|
| LightGunCommando（esp-idf#10504，Wii Remote 用户实测） | bonded 手柄重连由**栈自动完成**（incoming ACL → 栈自己 SDP → `OPEN_EVENT is_orig=false`），app 调 `dev_open` 反而打断；"don't call esp_hidh_dev_open, just handle OPEN_EVENT" |
| RAPOO BT3.0 键盘（esp32.com） | 重连 SDP 响应迟到 ~35s 仍成功；期间不得有第二次 outbound 尝试打断 |
| Espressif #15379 patch | host 侧主动重连 = 对已知 bda 定期 `dev_open`（probe） |
| ESP32Wiimote（HCI 直驱） | fast-reconnect 窗口 + **严禁 in-flight 时发起新连接** |
| BT 协议 spec | inquiry response = 对端 inquiry-scan 已开；gamepad 类的 inquiry-scan **只在**配对/广播态开 |

## 三、最终设计（main.c 单文件）

### 3.1 状态与常量

- 沿用 PR #1 三态机 `ST_SCANNING / ST_CONNECTING / ST_CONNECTED`。
- `CONNECT_TIMEOUT_MS 35000`：容忍栈内 SDP 迟到（参考表 2）。
- `SLOW_PROBE_MS 8000`：stuck-pad 出站探测节流（仍保留，但触发频次极少）。
- `PROBE_DELAY_MS 200`：OPEN FAIL 后快速 probe 的让出延迟。
- `g_last_probe_ms`、`g_probe_after_fail_bda`。
  **移除**（后续清理）：`g_candidate_fresh`、`g_force_fresh_next`、3s/8s 锁定窗口常量——
  inquiry 命中本身即 fresh 意图且必然立即 open，标志与窗口都成了死代码。

### 3.2 各触发路径

| 事件 | 行为 |
|---|---|
| inquiry 命中 candidate（**任意 bda**） | lock_tick **立即 open**（无锁定窗口），open 前 **wipe 该 bda 的 stale bond**（§3.4）→ SSP <1s 发起 |
| inbound `ACL_CONN_CMPL` | **只 log，不 dev_open**（被动让栈跑完 incoming SDP → auto OPEN） |
| `ACL_DISCONN_CMPL` 且 `g_state==ST_CONNECTED` | 复位 `ST_SCANNING` + rescan（手柄突然掉电时 CLOSE_EVT 可能不来，靠这条兜底） |
| `OPEN_EVENT` OK | `ST_CONNECTED` + dump descriptor |
| `OPEN_EVENT` FAIL | 单次 `remove_bond_device`（此刻 ACL 刚断，deferred delete 随即落地）+ arm 200ms probe timer |
| probe timer 到期 | `ST_SCANNING` 且距上次 probe ≥8s → `start_connect(bda,"probe",wipe)`；post-fail probe wipe=true，slow probe wipe=false；否则 `arm_rescan` 回 inquiry |
| scan round 空闲 ≥8s 且有 bond | lock_tick 里 slow probe 兜底（同节流） |
| `dev_open` 同步返回 NULL（TAILQ 残留） | 立即 stop conn_timeout + `reset_scan_state` + `arm_rescan`，不傻等 35s |

### 3.3 inquiry-driven fresh 原理（关键）

BT 协议语义：inquiry response ⇒ 对端开了 inquiry-scan。Flydigi Apex5 / X-input 手柄只在**配对模式**才开
inquiry-scan（普通 idle 仅 page-scan，对 inquiry 不响应）。所以 inquiry 命中 = 对端在**等配对**。

- **首次命中**：当帧 `set_candidate` → lock_tick 下一次触发（≤250ms）`open_candidate` →
  `dev_open` fresh SSP Just Works → `AUTH_CMPL stat=0 OK lk=4` → OPEN OK。
- **stale NVS key 不再作为 fresh/bonded 判据**：原 `g_candidate_fresh = !is_known_bonded_bda(bda)`
  只看本端 NVS，与对端真实 key 状态不同步。配对模式下对端 key 通常已清（旧 key 用 page 老 host
  用，新 host 走 inquiry 触发 fresh SSP），所以本端有没有 bond 都该走 fresh。
  key 的正确性改由 open 前的 wipe_bond 保证（§3.4），candidate 路径不再查询 bond 表。
- **3s EWMA 锁定窗口消除**：配对模式下手柄 inquiry-scan 通常 ~30s 后超时，越晚发起越容易错过；
  fresh 立即 open 是唯一不丢配对窗口的策略。EWMA + hysteresis 仍保留——多 gamepad 同时被
  discovery 时用于稳定选择，但单 gamepad 场景下不影响。

### 3.4 wipe-bond-before-open（v1 修复 241277d 移植，解决"第三次长按卡连接中"）

3.3 的 fresh 判定只控制 open **时机**，不控制 link 层 auth **用什么 key**：只要本端 NVS 还
有该 bda 的 bond，`dev_open` 触发的鉴权就会带旧 key；手柄清 key 进配对模式后会直接 0x05
拒绝（`ConnectCfm Status:5`），且手柄 auth-reject 后**会关闭全部 scan**，UI 永久卡"连接中"，
主机侧无法再触达——所以关键不是事后补救，而是**让 0x05 压根不发生**。

修复：inquiry 命中（=配对意图）或 OPEN-FAIL 后 probe 时，`start_connect(wipe_bond=true)` 在
`dev_open` 前先 `esp_bt_gap_remove_bond_device`。依据（源码取证）：

- `bta_dm_remove_device`：ACL down 时**立即** `BTM_SecDeleteDevice`（清 RAM link key + NVS）；
  ACL up 时 defer 到 link-down。outbound open 时必然无该 peer 的 ACL → remove 立即生效。
- BTC/BTA 消息队列 FIFO：remove 排在 dev_open 的 auth 之前处理 → auth 无 key → SSP Just
  Works → 与首次成功路径同型。

配套纠错：OPEN-FAIL 里旧的 `remove_bond ×3 同步重试 + bonds 复验` 是误解产物——重试只会看到
stale count。现状为**单次 remove + 200ms probe**：deferred delete 在链路断开时自动落地，
且 `start_connect(wipe_bond)` 在每次 open 前独立兜底，无需轮询复验。slow probe（`first_bonded_bda`
路径，合法 bonded 探测）不 wipe，所以 FAIL 路径的 remove 仍需保留（防止 stale 键被 slow probe
带回）。

实测（90s capture，连 3 次长按复现流程）：三次均 inquiry 命中 → wipe → 1~2.5s `AUTH_CMPL OK`
→ open，无"连接中"卡死。

### 3.5 防风暴约束

- probe 全部经 `g_last_probe_ms` 8s 节流；inquiry 在 probe 间隙照常运行（re-pair 需要它）。
- `start_connect` 有 `ST_CONNECTING` guard，in-flight 期间任何重复 open 直接忽略。
- `halt_scanning_side_effects` 中 cancel/stop 均为幂等 + `(benign)` log。

## 四、实机验证记录（2026-09-07）

### 4.1 Bug 复现（fix 前，90s capture）

```
+2.9   L2CAP ConnectCfm_Cb CID 0x0041 Status:4   ← outbound PAGE_TIMEOUT（手柄已进配对模式）
+10.1  ConnectCfm_Cb CID 0x0040 Status:4         ← probe 重试 PAGE_TIMEOUT
+10.1  open FAIL 0xffffffff, remove_bond→bonds=0
+15.6  ConnectCfm Status:4
+22.7  ConnectCfm Status:4
+27.4  hcif conn complete hdl 0x81 st 0x0        ← 手柄自发 inbound page（pad 内部 page timeout 周期）
+28.0  AUTH_CMPL stat=0 OK lk=4                  ← SSP Just Works
+28.1  open OK, descriptor dump, INPUT 流开始
```

整轮 ~28s 才连上（≈手柄 page retry budget）；手柄 UI 在这 ~28s 内**卡"连接中"**直到 inbound 才解。

### 4.2 Fix 后

`g_candidate_fresh`/锁定窗口移除后，命中即 open：

```
T+0     inquiry 命中 b55d...5475
T+~0.25 lock_tick → candidate fresh → open immediately
T+~1   dev_open → AUTH_CMPL OK → OPEN OK
```

手柄 UI ~1s 内"连接中"消失。占空窗口与手柄 inquiry-scan 时长绑定（~30s），不再被 EWMA 锁 / outbound
PAGE_TIMEOUT 拖延。

## 五、已知边界 / 后续

### 5.1 half-open inbound ACL 黑洞（挂账，仅调试操作可达）

三个必要条件**同时**成立才触发（缺一即正常）：
1. 非对称键：host 无该 bda 的 bond 且对端手柄持旧 key（唯一生产者 = `erase_flash` 时手柄还开着）；
2. 手柄的 inbound page **先于** host 的 inquiry 命中落地 → ACL 建立但双方都不鉴权，链路
   "半开挂住"（实测存活 ~47s，手柄自拆）；
3. 挂住窗口内 host 恰好 fresh open 该 bda → BTM 复用半开链路发 SDP（PSM 1，
   `BTM_SEC_BEST_EFFORT` 不触发鉴权）→ 手柄装死不应 → 35s conn_timeout + hidh wrapper 泄漏
   （公开 API 无法回收：`esp_hidh_dev_free` 是 no-op，`dev_close` 要求 connected），
   后续 open 全被 stale-TAILQ guard 弹回，直到栈自己 ~60s SDP 超时拆链，共 ~40-70s 黑洞。

**常规操作不可达性论证**（2026-09-08，4 轮实验对照）：拔模块电/手柄断电重上电 → 键对称，走
bonded 被动重连；手柄长按 pair → 手柄清**自己**的键（条件 1 方向反了，无键手柄不会 page 挂链），
host 侧残留 bond 由 §3.4 wipe_bond 消灭——001916/002718/234337 共 18 轮 pair/重连全部
0.5~2.6s 成功；FAIL 残留的非对称态也被"FAIL remove + open wipe"双向收敛（001916 +2.572
inbound 旧键 page → 一次 FAIL → 1.5s 后干净 SSP）。

**复现配方**（如需再取证）：保持手柄 connected 状态直接 `erase-flash` + 重烧 + 立即 capture
（手柄断链后的 page 风暴会抢先落地），风暴存活期内长按 pair。

**处置**：不修（public API 无强鉴权/拆链手段；候选方案"bonds 计数闸门关 page-scan"经论证对
"NVS 有其它设备 bond"变体仍漏，且常规操作本就不触发）。AGENTS 测试规范新增：*erase-flash 前先关手柄*。

### 5.2 其它

- 场景 4（board 断电重启 + 手柄仍 bonded）的 audit §四 死锁不在本 PR 范围
  （需 btc 层 patch `btc_storage_load_bonded_hid_info`，直接 app 调用会崩栈）。
- 多 gamepad 同时出现时，EWMA 平滑仍然工作；hysteresis 3dB 用于不同 bda 之间切换，但单 gamepad 场景不再触发（首条命中立即 fresh open）。
- HID report 字段解码仍是 raw hex（后续 PR）。
- 后续 MTM：连续 5 轮 power-cycle 稳定性、覆盖 inquiry-scan 30s 边界（连续 5 次都等到最后 1s 才命中）。

## 六、涉及文件

- `bluetooth/esp32-wroom-32e/apps/default/main/main.c`（唯一代码改动）
- 本 spec