# `apps/default` 连接流程审计与未覆盖场景清单

> **用途**：本文是**重设计 `apps/default` 连接流程**的输入——列出所有合理可枚举的状态/用户操作/配对途中事件，标注当前架构能否在"可接受时间"内恢复，作为下一步整体重设计的事实基础。不是执行文档。
>
> **配套**：
> - 现状 MTM：`docs/testing/apps-default-manual-tests.md`（7 个已编目场景，含场景 7 asymmetric-bond 已知未修）
> - 设计 spec：`docs/superpowers/specs/2026-09-06-bt-hid-callback-refactor-design.md`（§十二/§十三/§十五 验证记录）
>
> **原则**（来自项目负责人）：
> 1. 用户可能做各种合理操作，模块都必须在"可接受时间"内连上。
> 2. 配对/连接**途中**断电（手柄或模块）虽无法精确测试，流程设计必须覆盖。
> 3. **不再打补丁，整体重设计**。

---

## 一、状态空间（正交维度）

| 维度 | 取值 |
|---|---|
| **模块 NVS link key** | `none` / `has(bda)`（飞智八爪鱼5 单键场景） |
| **Bluedroid controller volatile key table**（模块断电即清零；每次 `bt_stack_start` 是全新 table，**v6.0.2 无公开 API 从 NVS 装回**） | `none` / `has(bda)` |
| **手柄端 link key** | `none` / `has(board_bda)` |
| **手柄状态机** | `power_off` / `booting` / `idle_unpaired` / `pairing_discoverable` / `connected_to_X`（X=board or other）/ `bonded_reconnect_pageonly` / `bonded_reconnect_failed_auth` |
| **模块状态机** | `controller_down` / `controller_up_idle` / `scanning_inquiry` / `scanning_probe` / `connecting_outbound` / `connecting_inbound_acl` / `connected` |
| **链路** | `none` / `acl_up_no_hid`（pad page 我们但 controller 没装 HID host）/ `acl_up_with_hid` / `acl_down` |

---

## 二、用户操作 + 配对/连接途中事件

### A. 完整路径
1. **fresh pair**：模块无键 + 手柄无键 + 手柄进 pairing_discoverable
2. **bonded reconnect**：模块有键 + controller 有键 + 手柄 bonded_reconnect_pageonly → 手柄 page 我们 → ACL+HID
3. **re-pair**（手柄侧清键）：模块有键 + 手柄用户长按配对键 → 手柄进 pairing_discoverable → 模块 probe 命中
4. **paired 后模块重启**（手柄不动）：模块断电→上电，controller 清零，手柄 bonded_reconnect_pageonly
5. **paired 后手柄重启**（模块不动）：手柄断电→上电 → bonded_reconnect_pageonly → page 我们
6. **paired 后模块重启 + 手柄仍 bonded_reconnect**（同 #4，独立列出因组合名不同）
7. **paired 后手柄 + 模块同时断电**

### B. 配对/连接**途中**断电（无法精确复现，流程必须覆盖）
8. fresh pair SSP 中段手柄断电（已发 PIN_REQ/KEY_REQ 未回 / AUTH_CMPL 未回）
9. fresh pair ACL 建上但 HID 未建时手柄断电
10. bonded reconnect ACL 建上但 AUTH 未完时手柄断电
11. bonded reconnect 正常运行时手柄断电
12. bonded reconnect 正常运行时模块断电
13. bonded reconnect 正常运行时手柄 + 模块同时断电
14. re-pair 进行中（手柄长按清键后、ACL 建上后）手柄断电
15. re-pair 进行中模块断电

### C. 用户误操作 / 极端状态
16. **模块长时间断电（>手柄省电超时，如 10 分钟）**后上电：手柄 page-only 失败 N 次后**是否切 discoverable / 清键**（厂商行为，Apex5 待实测）
17. 手柄长时间无操作：手柄 sleep/关机 → 模块 close
18. 手柄"连接中"卡住时用户**反复长按配对键**（试图重置）
19. **connected 时用户反复 USB 拔插模块**（同一会话内多次）
20. 手柄蓝牙切到其他设备（手机/电脑）：手柄侧清旧 host key → 进 discoverable
21. 手柄**切换连接模式**（X-input ↔ Switch Pro ↔ FlashPlay）：名字和 MAC 都可能变 → 白名单过滤掉
22. 模块 `erase_flash`：NVS 全清 + controller 全清 → 等同 fresh pair
23. **模块部分 NVS 擦**（如仅擦非蓝牙 namespace）：NVS 有 link key、controller 无 key、手柄有 key
24. **`esp_bt_controller_disable + enable`**（不擦 NVS）：仅 controller 清零（等同 #4）

---

## 三、当前架构能力矩阵（已实测 / 代码审计）

> ✅=已实测自动恢复；△=部分恢复 / 有副作用；✗=死锁或不可接受时间

| # | 用户操作 | 当前能否恢复 | 当前耗时/失败模式 | 备注 |
|---|---|---|---|---|
| 1 | fresh pair | ✅ | ~3s | 已实测 |
| 2 | bonded reconnect（手柄断电→上电） | ✅ | ~2-4s | probe 探询 + outbound page |
| 3 | re-pair（手柄长按） | ✅ | ~1.5-2.5s | probe 命中 → drop bond + 全新 SSP |
| 4 | **paired 后模块重启（手柄不动）** | ✗ | **死锁** | NVS 有键 + controller 无键 + 手柄有键；手柄 page-only 拒空 key（实测 ACL_CONN → dev_open → AUTH FAIL stat=10） |
| 5 | 手柄断电→上电（同会话） | ✅ | ~2-4s | controller 仍热，bonded page |
| 6 | 同 #4（独立列出因组合名不同） | ✗ | 同 #4 | |
| 7 | 同时断电 | △/✗ | 取决于谁先 ready | 接近 #4 或 fresh pair |
| 8 | fresh pair SSP 中段手柄断电 | △ | controller 收 disconnect → close 路径处理；后续靠 #1 | |
| 9 | fresh pair ACL 建上但 HID 未建时手柄断电 | △ | 同 #8 | |
| 10 | bonded reconnect AUTH 未完手柄断电 | △ | 同 #8 | |
| 11 | bonded reconnect 运行手柄断电 | ✅ | close → backoff → #5 | |
| 12 | bonded reconnect 运行模块断电 | ✗ | #4 | |
| 13 | 同时断电 | ✗ | #4 | |
| 14 | re-pair 进行中手柄断电 | △ | close → #1（discoverable 没建立） | |
| 15 | re-pair 进行中模块断电 | ✗ | #4 | |
| 16 | 模块长时间断电后上电 | △ | 若手柄 page-only 失败 N 次后切 discoverable → fresh pair；若手柄无限重试 → #4 | Apex5 行为**未实测** |
| 17 | 手柄 sleep | ✅ | close 事件 | |
| 18 | 手柄卡连接中反复长按 | ✅ | probe 反复命中 → 一致 | |
| 19 | connected 时反复拔插模块 | ✗ | 每次都是 #4 | |
| 20 | 手柄切其他设备 | △ | 手柄 discoverable → 我们 inquiry 找到 → fresh pair；NVS 旧 key 留作垃圾 | |
| 21 | 切连接模式（X-input↔Switch Pro） | ✗ | 新名字新 MAC → 白名单过滤 → 不连 | **建议后续 Task**（按模式重新配对） |
| 22 | erase_flash | ✅ | fresh pair | |
| 23 | 部分 NVS 擦 | ✗ | 等同 #4 | |
| 24 | controller disable+enable | ✗ | 等同 #4 | |

---

## 四、死锁收敛点

**所有 ✗ 行收敛到同一个状态组合**：
```
模块 NVS 有 link key (b5:...:75)
  + 模块 Bluedroid controller volatile key table = none（controller 刚 init 没装回 key）
  + 手柄有 link key（板子 bda）
  + 手柄 bonded_reconnect_pageonly（page 我们）
```

**触发路径**（都是用户合理操作）：
- #4/#6/#7：模块断电 / 重启
- #12：连接运行中模块断电
- #13：同时断电，模块先起
- #15：re-pair 中模块断电
- #16：长时间断电若手柄不放弃
- #19：connected 时反复拔插模块
- #23：部分 NVS 擦
- #24：controller disable+enable

**死锁根因**（Bluedroid v6.0.2 公开 API 限制）：
1. **没有公开 API** 把 NVS 里 Bluedroid 的 link key 灌回 controller volatile table（模块断电后 key 永远在 NVS，controller 永远不知道）。
2. **没有公开 API** 强制 controller 在入站 ACL 上"忽略对端旧 key、触发全新 SSP"（手柄在 bonded-reconnect 模式下收到空 key 的 dev_open 直接拒）。
3. **没有公开 API** 检测入站 ACL 后让对端"放弃 bonded-reconnect、进入 discoverable"（即便我们禁用 page-scan，依赖手柄厂商行为，**Apex5 是否会放弃未实测**）。

**修了收敛点 = 修了 ✗ 的 8 条**。

---

## 五、当前 ACL_CONN 处理（commit `e242cfa`，实测**无效**）

```
ACL_CONN_CMPL_STAT_EVT 触发 → 若 bonds==0 → 打印 + esp_hidh_dev_open(bda, BT, 0)
```

**实测结果**（commit `cb68343` 之后那次 60s capture）：
- `[gap] ACL_CONN inbound addr=b5:... bonds=0 -> forced dev_open` ✓（事件正确触发）
- `open FAIL 0xffffffff` + `AUTH_CMPL stat=10 FAIL lk=0` ✗（手柄拒了空 key 的全新 SSP）
- 死锁没解

**根因**：手柄在 bonded_reconnect_pageonly 状态下，收到空 key 的 dev_open → 用自己的 key 跟我们 controller 的空 key 配对失败 → 拒。**全新 SSP 路径在 bonded-reconnect 状态下不工作**。

---

## 六、重设计目标（给下一步 brainstorming）

> **目标**：在"任何模块断电/复位/拔插/NVS 部分擦 + 手柄任意状态"的组合下，模块在**可接受时间**内自动连上，或**最差清晰地告诉用户做哪一步**。

候选方案（在 brainstorming 里展开）：
- A. **NVS 持久化 + 启动时把 link key 灌回 controller**（v6.0.2 无公开 API，需内部符号/vendor HCI，侵入性大）
- B. **诊断 + 文档化**：检测到死锁状态组合后，清晰打印 + 引导用户操作（**最诚实**）
- C. **装糊涂**：禁用 page-scan 一段时间 → 手柄"放弃" bonded-reconnect → 进 discoverable → 我们 inquiry 找到（**依赖手柄厂商行为，未实测**）
- D. **检测到死锁 → `remove_bond_of(bda)` 清 NVS + 永久 discoverable 等用户长按手柄**：把状态从"两边各持无效 key"变成"模块无 key、等手柄 discoverable"（**用户操作一次即可，且清掉垃圾 NVS key**）
- E. **B + D 组合**（推荐起点）：不假装能自动修所有路径，坦诚引导用户 + 清掉 NVS 无效 key 避免复现

---

## 七、待项目负责人确认

1. **状态/事件/操作列表**（§二）有没有遗漏？特别是配对/连接途中断电（§B）和极端状态（§C）。
2. **§三矩阵的"当前能否恢复"判定**同意吗？特别是：
   - #4/#6/#19 你确认是真实场景？
   - #16（长时间断电）Apex5 在 page-only 失败 N 次后会不会清键 / 切 discoverable？
   - #21（切连接模式）需要现在处理还是记为后续 Task？
   - #23（部分 NVS 擦）是否实际会发生？
3. **重设计原则**（§六目标）同意吗？倾向哪个候选方案？

确认后我据此做整体重设计（不再打补丁）。
