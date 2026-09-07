# BT HID 场景 2：bonded 重连 + stale-bond 自动恢复 — 设计 spec

> **范围**：手柄断电再上电（board 不重启）能自动重连；手柄随时长按配对键 re-pair 都能成功；
> 全程**不需要**手动 `erase_flash` 复位模块。
>
> **基线**：PR #1 merge 后 `main HEAD d9c67e7`。
>
> **状态**：已实机验证（2026-09-07，300s 连测：8 次 open OK / 0 crash / 卡死均在 12–23s 自动恢复）。

## 一、问题链（实测逐层暴露）

1. **PR #1 无任何重连路径**：inbound ACL 只 log；bonded 手柄不可被发现 → inquiry 永不命中。
2. **配对窗口丢失**：candidate 命中后还要等 stable 3s / total 8s，手柄退出配对态后连不上。
3. **auto-reconnect 被 app 抢跑打断**：inbound 后 app 立刻 `dev_open`，与栈自己的 SDP 程序
   竞争（ESP32 一次只跑一个 SDP，esp-idf#10504）→ `ERR_SDP` 风暴。
4. **stale-bond 死循环（本 spec 核心）**：手柄长按配对键清掉了自己的 key，board NVS/BTM
   还留着旧 key → 之后所有连接尝试 auth 失败；且
   `esp_bt_gap_remove_bond_device()` 返回 ESP_OK 但
   `esp_bt_gap_get_bond_device_num()` 不减（BTC/BTM 缓存未清）→ 单纯清键不可靠。
   手柄同时耗尽 page 预算，卡"连接中"，不再主动 page。

## 二、参考项目证据链

| 项目 | 结论 |
|---|---|
| LightGunCommando（esp-idf#10504，Wii Remote 用户实测） | bonded 手柄重连由**栈自动完成**（incoming ACL → 栈自己 SDP → `OPEN_EVENT is_orig=false`），app 调 `dev_open` 反而打断；"don't call esp_hidh_dev_open, just handle OPEN_EVENT" |
| RAPOO BT3.0 键盘（esp32.com） | 重连 SDP 响应迟到 ~35s 仍成功；期间不得有第二次 outbound 尝试打断 |
| Espressif #15379 patch | host 侧主动重连 = 对已知 bda 定期 `dev_open`（probe） |
| ESP32Wiimote（HCI 直驱） | fast-reconnect 窗口 + **严禁 in-flight 时发起新连接** |

## 三、最终设计（main.c 单文件）

### 3.1 状态与常量

- 沿用 PR #1 三态机 `ST_SCANNING / ST_CONNECTING / ST_CONNECTED`。
- `CONNECT_TIMEOUT_MS 35000`：容忍栈内 SDP 迟到（参考表 2）。
- `SLOW_PROBE_MS 8000`：stuck-pad 出站探测节流。
- `PROBE_DELAY_MS 200`：OPEN FAIL 后快速 probe 的让出延迟（等当前 HID 事件排空）。
- `g_candidate_fresh`（本次命中是否非 bonded）、`g_force_fresh_next`（一次性的
  "NVS 清不动也当 fresh 处理"标志）、`g_last_probe_ms`、`g_probe_after_fail_bda`。

### 3.2 各触发路径

| 事件 | 行为 |
|---|---|
| inquiry 命中 candidate（bda **不在** NVS bond 列表） | `fresh` 标记；lock_tick **立即 open**（不等 stable/total）→ 配对窗口内 <1s 发起 SSP |
| inquiry 命中 candidate（bda **在** NVS bond 列表） | 走原 stable wait（3s/8s）再 outbound `dev_open` |
| inbound `ACL_CONN_CMPL` | **只 log，不 dev_open**（被动让栈跑完 incoming SDP → auto OPEN） |
| `ACL_DISCONN_CMPL` 且 `g_state==ST_CONNECTED` | 复位 `ST_SCANNING` + rescan（手柄突然掉电时 CLOSE_EVT 可能不来，靠这条兜底） |
| `OPEN_EVENT` OK | `ST_CONNECTED` + dump descriptor |
| `OPEN_EVENT` FAIL | `remove_bond_device` 最多 3 次（带 `bonds=` 复验 log）；残留则 `g_force_fresh_next=true`；随后 **arm 200ms probe timer** |
| probe timer 到期 | `ST_SCANNING` 且距上次 probe ≥8s → `start_connect(bda,"probe")`；否则 `arm_rescan` 回 inquiry |
| scan round 空闲 ≥8s 且有 bond | lock_tick 里 slow probe 兜底（同节流） |
| `dev_open` 同步返回 NULL（TAILQ 残留） | 立即 stop conn_timeout + `reset_scan_state` + `arm_rescan`，不傻等 35s |

### 3.3 stale-bond 自愈原理（关键）

`remove_bond_device` 对 BTC 缓存无效，但**出站 probe page 成功通过 auth 时，
BTM 会自然处理 key 不匹配并刷新双方链路状态**；auth 失败路径（inbound，栈处理）
之后 NVS 检查在下一轮 `candidate_update` 里已因手柄清键而不匹配。
实测：`bond persists` → `post-fail probe` → **12–23s 内 `AUTH stat=0 OK → open:`**，
之后 fresh/bonded 交替路径恢复正常循环。

### 3.4 防风暴约束

- probe 全部经 `g_last_probe_ms` 8s 节流；inquiry 在 probe 间隙照常运行（re-pair 需要它）。
- `start_connect` 有 `ST_CONNECTING` guard，in-flight 期间任何重复 open 直接忽略。
- `halt_scanning_side_effects` 中 cancel/stop 均为幂等 + `(benign)` log。

## 四、实机验证记录（2026-09-07）

300s capture（不擦 NVS，复用上轮脏状态）：

```
T+26  candidate fresh → open immediately → T+28.9 open（场景1 <3s）
T+44  close → T+47 candidate bonded → T+52.5 auto open（场景5 ✓）
T+71  open FAIL → remove_bond×3 bonds=1 → force_fresh → post-fail probe
      → T+83.5 AUTH stat=0 OK → open（卡死 12s 自愈 ✓）
T+101/183/278 三次 re-pair 均 fresh <2s 连上（场景3 ✓）
合计：open OK ×8、input 8819、crash 0、无永久卡死
```

回归通过：MTM 场景 1（fresh pair）、场景 3（re-pair）、场景 5（power-cycle 重连）。

## 五、已知边界 / 后续

- 场景 4（board 断电重启 + 手柄仍 bonded）的 audit §四 死锁不在本 PR 范围
  （需 btc 层 patch `btc_storage_load_bonded_hid_info`，直接 app 调用会崩栈）。
- 手柄长按配对键后的 ~3s 内如恰逢 outbound probe in-flight，会被 auth 拒绝打断一次，
  8s 节流 + 下一轮 fresh candidate 立即连，用户无感。
- HID report 字段解码仍是 raw hex（后续 PR）。
- 建议后续加 MTM 场景：连续 5 轮 power-cycle 稳定性、覆盖边缘（远离后走 `probe` 失败→回 inquiry）。

## 六、涉及文件

- `bluetooth/esp32-wroom-32e/apps/default/main/main.c`（唯一代码改动）
- 本 spec
