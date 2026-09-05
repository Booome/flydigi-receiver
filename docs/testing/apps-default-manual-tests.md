# `apps/default` 手动回测手册（MTM）

> **范围**：本文件列出 `bluetooth/esp32-wroom-32e/apps/default/`（BT HID 主机）的**手工回测场景**。这些场景涉及真机（飞智八爪鱼5 手柄） + 用户操作，**不能自动化**；每次改完固件或做大改动前都应按本表跑一遍。脚本只覆盖 `build/burn/capture` 三件事。
>
> **配套规范**：
> - 设计 spec：`docs/superpowers/specs/2026-09-06-bt-hid-callback-refactor-design.md`（§十二、§十三、§十五为验证记录与衍生 fix）
> - 实施计划：`docs/superpowers/plans/2026-09-06-bt-hid-callback-refactor.md`
> - 代码：`bluetooth/esp32-wroom-32e/apps/default/main/{main.c,bt_stack.c,hid_report.c}`
>
> **硬件前提**：ESP32-WROOM-32E（board_a），`.env` 已配 `BOARD_A_PORT` + `BOARD_A_TYPE=esp32-wroom-32e`；手柄拨到蓝牙 X-input 模式（蓝灯）。
>
> **通用命令**：
> ```bash
> source /opt/esp-idf/export.sh
> python3 bluetooth/esp32-wroom-32e/tools/build.py --app default
> python3 bluetooth/esp32-wroom-32e/tools/burn.py --board-a
> python3 tools/capture_uart.py --board-a --rst-a --duration N --odir /tmp --ts
> ```
> 看 lifecycle 用 `grep -aE "boot bonds|outbound page|candidate|connecting:|AUTH_CMPL|open FAIL|\[hid\] open:|bond removed|connect timeout|fails to|\[hid\] close:" <log>`。
> 提醒：手柄空闲 10–30s 省电，**每次开始手动测试前先让其进入配对态（蓝灯快闪）**。

---

## 场景 1：全新首配（无 bond）

- **前置**：板子 `bonds=0`；手柄在配对态（蓝灯快闪）。
- **步骤**：起 capture → **同时**开手柄电源 → 等 5–10s。
- **期望**：
  - log：`[hid] candidate: addr=b5:... smoothed=-5x` → `connecting:` → `[gap] AUTH_CMPL ... stat=0 OK lk=4` → `[hid] open:` → `[hid] state:` 持续。
  - 时延：**candidate → open ≈ 2–4s**（含 EWMA 稳定窗 3s）。
- **相关 fix**：无（首次实现即正确）。

## 场景 2：Bonded 重连（手柄在 bonded-reconnect page-only）

- **前置**：场景 1 完成后（板子 + 手柄都已绑定）；手柄**关机再开机**。
- **步骤**：起 capture → 等几秒手柄开机 → 让手柄自然走 bonded-reconnect。
- **期望**：
  - log：`outbound page bonded b5:...` → `open:`。**Probe 探询会先跑 ~2.56s 找不到**（手柄 page-only 不可被发现），然后 DISC_STOP 回退到 outbound page。
  - 时延：**close → open ≈ 2.5–4s**（probe 等待 + page）。
- **相关 fix**：bonded-probe 启发式（§十三 / commit `241277d`）。

## 场景 3：Re-pair（手柄被强制重新配对，pad 端清键）

- **前置**：场景 1 完成后；手柄**长按配对键**重新进入配对态（pad 清旧键、变可被发现）。
- **步骤**：起 capture → 让手柄进入配对态。
- **期望**：
  - log：`[hid] candidate: addr=b5:...` → `[hid] connecting:` → `[gap] AUTH_CMPL ... stat=0 OK` → `[hid] open:`。
  - 关键：此路径**不应**走 `outbound page bonded`（手柄已清键，page 会失败）。Probe 探询找到手柄（inquiry 可见）→ `remove_bond` → 全新 SSP。
  - 时延：**candidate → open ≈ 1.5–2.5s**。
- **相关 fix**：bonded-probe 启发式（§十三 / commit `241277d`）；g_probe 守卫（§十五 / commit `3499bfe`、`ee05752`）。

## 场景 4：反复 re-pair（用户视角一致性测试）

- **前置**：场景 1 完成后。
- **步骤**：起 120s capture → 在 capture 内连续做 3–5 次"手柄长按配对键重进"。
- **期望**：每次 re-pair 行为一致，~1.5–2.5s 内连上；**无任何卡死、无重启循环**。
- **实测**：commit `241277d` 实测 7 次连接、0 死锁、0 重启；详见 spec §十三。
- **相关 fix**：bonded-probe 启发式。

## 场景 5：Power-off / Power-on（绑定保持时的优雅重连）

- **前置**：场景 1 完成后；手柄开机且已连接。
- **步骤**：手柄**关机**（注意：手柄上"开机+长按配对键"=清键，与本场景不同）→ 等 3–5s → **再开机**（普通开机，不是长按配对）。
- **期望**：
  - log：捕获到一次 `close:`（手柄关机断链）→ 数次 `outbound page bonded` + `connect timeout`（手柄还开着）→ 手柄开机后 `open:`。
  - 时延：手柄开机 → 板子 outbound page 命中 → **约 1–4s 内连上**。
  - **关键**：板子 `bonds` 必须保持 =1（不允许裸 timeout 清键）。本次 fix（commit `cb68343`）专门修这点。
- **相关 fix**：`note_connect_fail` 不再在裸 timeout 清键（commit `cb68343` / spec §十五）。

## 场景 6：Stale-key 自动恢复（板子有旧键，手柄已重配对）

- **前置**：场景 1 完成后；手柄长按配对键重进（pad 清键）。
- **步骤**：观察 log；无需用户额外动作。
- **期望**：
  - log：`outbound page bonded` → `open FAIL: ... status=0xffffffff` → `[hid] bond removed for ...` → `[hid] candidate:` → `[gap] AUTH_CMPL stat=0 OK` → `[hid] open:`。
  - 时延：~3–6s 内自动恢复（OPEN FAIL → remove_bond → inquiry → 全新 SSP）。
- **相关 fix**：OPEN FAIL 路径已有；probe 启发式也加速了此路径。

## 场景 7（已知未修复）：Asymmetric bond — 板子 bonds=0、手柄仍有键

- **症状**：手柄开机后**永远卡"连接中"**。log 特征：
  - `[hid] boot bonds=0`
  - `BT_HCI: hcif conn complete: hdl 0x8x, st 0x0`（手柄 page 我们，ACL 建上）
  - ~10s 后 `BT_HCI: hcif disc complete: hdl 0x8x, rsn 0x8`（link supervision timeout 拆掉）
  - 循环出现 2 次以上 → 死锁。
- **根因**：v6.0.2 Bluedroid **没有公开 API**让应用层感知入站 ACL 的 BDA；我们应用层在做 inquiry（bonds=0 → `begin_scan_round`），没人去 `dev_open` 那个入站 ACL，ACL 空挂超时 → 拆 → 重 page → 死循环。
- **临时恢复**：**长按手柄配对键强制重进配对**（让手柄清键、变可被发现）→ 自动恢复。
- **状态**：**未修**。修法需 Bluedroid 内部访问或硬编码手柄 BDA 周期 dev_open，风险/收益评估中。
- **预防**：尽量**不要让板子端 NVS bond 丢失**（不要 `erase_flash`、不要进 `nvs_flash_erase`）。当前所有 fix（commit `82bd08e`…`cb68343`）都保证了正常路径不丢键。
- **如何识别**：看到 log 里 `hcif conn complete st=0x0` 紧跟着 `hcif disc complete rsn 0x8`，且应用层没有 `dev_open` → 就是这个场景。

---

## 历史回归记录（按 commit）

| Commit | 修复/变更 | 覆盖场景 |
|---|---|---|
| `140ef48` | `main.c` 改写为回调链 | 场景 1、2、3（基础） |
| `f6ef910` | resume_scan 回退 fix | 场景 2 的边缘 bond-list 失败 |
| `ed6fb7a` | gattc 回调恢复（esp_hidh 必需） | 启动门 |
| `82bd08e` | 删 esp_hid_gap | 启动门 |
| `241277d` | bonded-probe 启发式 | 场景 3、4（re-pair 一致性） |
| `3499bfe` + `ee05752` | g_probe 清零 + DISC_STOP 守卫 | 场景 3 的入站 page 边缘 |
| `cb68343` | 不在裸 timeout 清键 | 场景 5（power-off/on） |

## 任何修改后必跑的最小回归集合

按耗时从短到长（建议总时长 ~5 分钟 + 人工操作）：

1. **场景 1**（30s capture）：全新首配 → 应在 ~3s 内 `[hid] open:`。
2. **场景 5**（45s capture）：先连上 → 关手柄 → 等 3s → 开机 → 应在 ~3s 内重连。
3. **场景 3**（30s capture）：连上后长按配对键重配 → 应在 ~2s 内重连。

任一步未达预期 → 用 `git bisect` / 逐回退定位。
