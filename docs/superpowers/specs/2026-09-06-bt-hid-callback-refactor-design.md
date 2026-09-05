# BT HID 主机回调链重构设计（`apps/default/`）

## 一、背景与目标

前序：`bt-hid-report-decode` 已让 `apps/default/` 稳定连上飞智八爪鱼5 的 BR/EDR HID
并解码 15 字节输入报告。但整个连接生命周期由 `app_main` 里的 **`while(1)` 轮询状态机**
驱动：每圈 `vTaskDelay(200ms)` 轮询状态、检查 connect 超时/退避、并在需要时调用**阻塞式**
`esp_hid_scan(3s)`（其内部自成一个 `start → select() 收 gap 事件 → stop → 返回结果数组`
的同步循环）。

本里程碑：**去掉 `while(1)` 轮询，改为纯回调链驱动**——流程由 BT/HID 事件回调与
`esp_timer` 定时器逐段推进，不再有忙等循环。预期收益：

- **实时性更好**：inquiry 每个设备结果（`DISC_RES_EVT`）一到就喂进候选/EWMA 判定，
  而不是每 3s 攒一批统一处理；连接/断开结果即时驱动下一动作。
- **不忙等**：无 `while(1)+vTaskDelay` 轮询；空转时任务栈完全不占用。
- **事件即状态迁移**：状态迁移点与真实事件一一对应，易读、易加后续动作（震动/转发）。

**关键立场（防掩耳盗铃）**：实时性来自"事件到达即处理"这一真实机制变更，而非把打印提前。
重构必须保持既有已验证行为不回退：SSP Just Works 首配、bond 冷启动 outbound page 重连、
power-cycle reconnect、close/超时后回 discovery、stale-key 自动清键。

**非目标**：转发 PC / 虚拟手柄、HID Output（震动）、SLE、多设备并发、逐位映射修正
（按钮位映射的实测校正是另一独立任务，本重构**不动** `hid_report.{c,h}` 的解码内容）。

## 二、硬约束与根因

`while(1)` 里唯一"天生阻塞、无法直接搬进回调"的是 `esp_hid_scan`：它自成一个 select
循环并阻塞数秒，**不能在 BT/GAP 回调上下文调用**（会卡死协议栈事件任务）。因此要做到
"真·全回调链"，必须：

> **弃用 `esp_hid_scan` / `esp_hid_gap`，自己注册 GAP 回调，用 `esp_bt_gap_start_discovery`
> 异步发起 BR/EDR inquiry**，让结果以 `ESP_BT_GAP_DISC_RES_EVT` 逐条到达、就地处理。

已确认的可行性要点：
- `esp_hidh_dev_open/close`、HID `OPEN/INPUT/CLOSE` 事件**本就是异步回调**，直接留在链上。
- 从 GAP 回调（BT 任务）/ `esp_timer` 回调（timer 任务）调用
  `esp_hidh_dev_open` / `esp_bt_gap_start_discovery` / `esp_bt_gap_cancel_discovery` /
  `esp_bt_gap_remove_bond_device` 仅是向 BT 栈投递请求，安全（异步 BT 例程的标准用法）。
- 手柄是**单模 BR/EDR 输入**（`bt_scan` 实测 X-input 模式下 BLE 命中 0），本次回调链**只跑
  BR/EDR 的发现/连接流程**。但这**不等于关掉 BLE**：见 §四「BLE 定位」——底层仍以 BTDM
  初始化，BLE 能力保留为后续低成本 add-on。

- `esp_hid_scan` 的 EIR/名字解析逻辑（`handle_bt_device_result` 中 `esp_bt_gap_resolve_eir_data`
  取 `CMPL_LOCAL_NAME`/`SHORT_LOCAL_NAME`）搬到我们的 GAP 结果处理里。

## 三、技术栈

| 项 | 选型 |
|---|---|
| 平台 | ESP32-WROOM-32E（board_a），`BOARD_A_TYPE=esp32-wroom-32e`，沿用 |
| 框架 | ESP-IDF v6.0.2（`/opt/esp-idf` 只读）、Bluedroid、`esp_hidh`（HID 主机层保留）|
| 发现 | `esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, len, 0)` + `esp_bt_gap_cancel_discovery()` |
| GAP 回调 | 自写 `esp_bt_gap_register_callback(bt_gap_cb)`：`DISC_RES_EVT`/`DISC_STATE_CHANGED_EVT`/SSP(AUTH_CMPL/CFM_REQ/KEY_REQ/KEY_NOTIF/PIN_REQ)/MODE_CHG |
| 定时器 | `esp_timer` 一次性 + 自重投：`lock_tick`、`connect_timeout`、`rescan_backoff` |
| 归属 | **`apps/default/`**（主 app，不新建）|
| 构建/烧录/抓 log | `tools/build.py`（默认 `default`）、`tools/burn.py --board-a`、`tools/capture_uart.py --board-a --rst-a` |

## 四、模块划分（design for isolation）

拆分职责，`main.c` 不再独自背全部：

```
bluetooth/esp32-wroom-32e/apps/default/main/
├── main.c        # 编排 + 状态机(事件驱动) + 候选/EWMA/迟滞 + esp_timer 回调 + GAP/HID 回调
├── bt_stack.c    # 新增：底层 bring-up（controller+Bluedroid 以 BTDM 初始化+SSP 安全参数+scan_mode），
│                 #      只暴露 bt_stack_start()；不注册 gap 回调、不含扫描、本里程碑不接 BLE flow
├── bt_stack.h    # 新增：bt_stack_start() 声明
├── hid_report.c/.h  # 不变（解码/打印沿用）
├── esp_hid_gap.c/.h # 删除（其有用逻辑拆到 bt_stack.c + main.c）
└── CMakeLists.txt   # 改：SRCS 用 "bt_stack.c" 替换 "esp_hid_gap.c"，去 esp_hid_gap 依赖
```

- **`bt_stack.c`**：从 `esp_hid_gap.c` 抽出 `init_low_level` 的 **Bluedroid(非 NimBLE) 分支**
  （`esp_bt_controller_init/enable` 以 **`ESP_BT_MODE_BTDM`**、`esp_bluedroid_init_with_cfg/enable`）
  + `init_bt_gap` 里的安全参数部分（SSP `iocap=ESP_BT_IO_CAP_NONE`、`esp_bt_gap_set_pin`、
  `set_scan_mode(CONNECTABLE, NON_DISCOVERABLE)`）。**控制器模式保持 BTDM（BR/EDR+BLE 都使能）**，
  但本里程碑 `bt_stack.c` **不注册 BLE gap 回调、不启动 BLE 扫描/连接**。
  `bt_stack_start(void)` 完成上述，返回 `esp_err_t`。
- **`main.c`**：`app_main` 只做 `nvs_flash_init → bt_stack_start → 注册 gap 回调 + esp_hidh_init
  → boot bond 诊断 → 起步决策（`try_open_bonded()` 或 `start_discovery()`）→ 返回`，**无 `while(1)`**。
  候选/EWMA/迟滞/锁定函数、三个 esp_timer 回调、`bt_gap_cb`、`hidh_event_handler` 全在此。
  **必须保留** `esp_hidh_init` 前的 `esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler)`：
  `esp_hidh_init` 的 BLE 分支 `esp_ble_hidh_init()` 会 `esp_ble_gattc_app_register(0)` 后
  `WAIT_CB()`（`portMAX_DELAY`）等 gattc REG 事件，而该回调未注册则**永久阻塞启动**——这是
  esp_hidh 的强制初始化管道，**即便只用 BR/EDR 也需要**，不等于跑 BLE 业务流。
  （曾误当作"喂 esp_hid_scan 的信号量"删除，实测导致启动卡在 `[hid] boot bonds` 之前，已恢复。）

### BLE 定位（重要，防"以为删 BLE 代码=没 BLE 了"）
- **删 `esp_hid_gap.c` 里的 BLE adv/scan handler ≠ 关掉 BLE 射频**。那些是官方 example 胶水，
  我们从没用过；底层 Bluedroid 仍 **BTDM**、BLE 射频照常使能。
- 现有证据：`bt_scan` 实测 X-input 模式 **BLE 命中 0**；HID descriptor 另有 `id=2/4 INPUT(GENERIC)`
  + `id=3 OUTPUT` 的 **vendor 通道**——飞智改键/宏/OTA **很可能走 BR/EDR HID 的 vendor report**，
  而非 BLE。**但未确证**（安卓"游戏厅"可能另有 BLE GATT 路径）。
- 因此本重构**只把 BR/EDR 流程事件化**，**不实现 BLE 流程**，也**不削减 BLE 能力**。
- **日后确证需要 BLE**（config/OTA 走 GATT）：新增一个隔离的 `ble_gatt.{c,h}`
  + `esp_ble_gap_register_callback` + gattc 客户端，**完全不改**本 BR/EDR 回调链，属增量小改动。


> 分节顺序遵循 AGENTS.md：include→define→type→global→前向声明→函数定义。

## 五、状态机（事件驱动，无轮询）

保留 `ST_SCANNING / ST_CONNECTING / ST_CONNECTED`，但 `g_state` **只作动作护栏**
（例如 connected 时不得再 discovery），迁移全部由事件/定时器函数直接完成，不再被循环读取。

```
app_main
  bt_stack_start(); register gap cb; esp_hidh_init();
  if (esp_bt_gap_get_bond_device_num() > 0)  try_open_bonded()   // 有 bond → 直接 page
  else                                        start_discovery()   // 无 bond → inquiry

── ST_SCANNING ──────────────────────────────────────────────
 start_discovery(): cancel 若在建 → esp_bt_gap_start_discovery(GENERAL_INQUIRY, INQ_LEN, 0)
                   arm lock_tick(250ms 周期自重投)
 [GAP DISC_RES_EVT]  解析 bda/name/cod/rssi → is_gamepad(name)? →
                     ewma_update(bda) → 层3 迟滞/替换/候选计时（逻辑同现状，改为逐事件调用）
                     gp_count 维护（本轮 inquiry 命中的 gamepad 数）
 [GAP DISC_STATE=STOPPED] 仍处于 SCANNING → 立即 start_discovery() 续上（保持连续）
 [lock_tick 250ms]  静默期也重评锁定条件 →
                       锁定（条件严格等同现状：stable>=LOCK_WAIT_MS 或 total>=MAX_WAIT_MS）：
                       cancel_discovery + disarm tick →
                             set_state(CONNECTING) + arm connect_timeout(4s) + dev_open(cand)
──────────────────────────────────────────────────────────────

── ST_CONNECTING ────────────────────────────────────────────
 [HIDH OPEN ok]     disarm connect_timeout → note_connect_ok → set_state(CONNECTED)
                    （connected 下 discovery 已停、tick 已停）
 [HIDH OPEN fail]   disarm timeout → dev_free + remove_bond_of → backoff_to_scan()
                    → note_connect_fail（≥N 才二次清键/长退避）
 [connect_timeout 4s]  note_connect_fail(tried)（保 bond，除非累计到阈值）→ backoff_to_scan()
──────────────────────────────────────────────────────────────

── ST_CONNECTED ─────────────────────────────────────────────
 [HIDH INPUT]       hid_decode → memcmp 变化才 hid_print_state（不变）
 [HIDH CLOSE]       dev_free → backoff_to_scan()
──────────────────────────────────────────────────────────────

backoff_to_scan(): reset_to_scan(); arm rescan_backoff(RETRY_BACKOFF_MS, 单次)
[rescan_backoff 到期]  有 bond? try_open_bonded() : start_discovery()
```

- **三个 esp_timer**：均为一次性；`lock_tick` 到期在 handler 末尾**自重投**实现周期（仅在
  SCANNING 存活；迁移出 SCANNING 时 `esp_timer_stop` 并不再重投）。`connect_timeout`、
  `rescan_backoff` 单次、按需 arm/disarm。
- **并发护栏**：`lock_tick`（timer 任务）与 `bt_gap_cb`/`hidh cb`（BT 任务）会并发改
  `g_candidate`/`g_ewma`/`g_state`。用**一把互斥锁 `g_app_mutex`** 保护这些共享结构
  （`DISC_RES_EVT`、`lock_tick`、OPEN/CLOSE/timeout/backoff 处理进入时加锁、退出解锁）。
  临界区仅做内存级判定 + 调 BT API（非阻塞），无死锁风险。

## 六、候选算法搬迁

三层算法（EWMA α=0.3、迟滞 3dB、`LOCK_WAIT_MS 3000`、`MAX_WAIT_MS 8000`、
`CANDIDATE_GONE_ROUNDS`）**判定逻辑与常量保持不变**，仅换触发方式；`gp_count` 仅作诊断打印，
**不参与锁定决策**（与现状一致，本重构不新增"单候选抢先锁"等行为，保持行为等价）。

- 现状：每圈 inquiry 结束后，对 `best` 结果批量更新 EWMA、更新候选、算 `stable_elapsed/
  total_elapsed` 决定是否连。
- 新法：`is_gamepad(name)` 命中的**每条** `DISC_RES_EVT` 即时 `ewma_update + 候选层3`（选 `best`
  = 当前平滑值最高者）；锁定判定搬进 `lock_tick`（周期 250ms）评估，条件不变。
  `g_scan_start_ms` 在 `start_discovery` 时刷新；"候选 gone"由"候选最后一次被 DISC_RES 命中
  的时间戳"判定（连续模式无批量"未出现"概念），超过 `CANDIDATE_GONE_ROUNDS` 对应时长则清候选。

## 七、错误处理与重连（保持不回退）

| 场景 | 处理（与现状语义一致）|
|---|---|
| 首配无 bond | 连续 inquiry → Just Works（SSP iocap=NONE + KEY_REQ passkey=0 + CFM_REQ 确认 + PIN_REQ 回码，全搬自 esp_hid_gap）→ OPEN ok |
| 冷启动有 bond | `try_open_bonded()`：page `bond_list[0]` + arm connect_timeout |
| bonded pad 自己回连（page-only、inquiry 不可见）| 由 SCANNING 起步/退避时优先 `try_open_bonded()` 覆盖 |
| OPEN fail（对端拒绝=它 re-pair 忘了我们）| 立即 `remove_bond_of` → 退避 → 下轮 inquiry 全新配对 |
| connect 超时（对端只是缺席/关机）| **保 bond**；`note_connect_fail` 累计到 `CONNECT_FAIL_HINT_AFTER(3)` 才清键+长退避 |
| close（手柄睡眠/断电）| 退避 → 回 SCANNING（有 bond 则先 page）|

## 八、Kconfig / 依赖

- **SSP 常开**：`bt_stack.c` 直接设置 SSP `iocap=NONE` + 各应答，**不再依赖 example 的
  `CONFIG_EXAMPLE_SSP_ENABLED`**（`Kconfig.projbuild` 里那条可保留不用；`bluedroid_cfg.ssp_en`
  维持现状默认开）。
- **sdkconfig 保持现状（BTDM、`CONFIG_BT_BLE_ENABLED=y`）**：不削减 BLE 能力，控制器仍双模使能
  （见 §四「BLE 定位」）。本里程碑只是**不跑 BLE 业务流程**，**不改射频配置**。
- `main/CMakeLists.txt`：`SRCS` 用 `bt_stack.c` 替换 `esp_hid_gap.c`；`REQUIRES`/`PRIV_REQUIRES`
  保持 `bt`、`esp_hid`；不再需要 esp_hid_gap 头。
- `esp_hidh` 本身仍支持 BT+BLE；`esp_hidh_init` 内部会初始化 BLE HID 主机路径，故 `main.c`
  **保留** `esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler)`（见 §四），日后真要接
  BLE HID 也无需换栈，只补 `ble_gatt` 业务模块即可。

## 九、验证矩阵

前置：board_a 已连、`.env` 有 `BOARD_A_PORT` + `BOARD_A_TYPE=esp32-wroom-32e`；每次测试前
提醒用户手柄进配对态（见 AGENTS.md，手柄 10–30s 省电）。

| # | 步骤 | 通过标准 |
|---|---|---|
| 1 | `build.py --app default` | 编译通过、无新 warning |
| 2 | `clang-format --dry-run --Werror` | 全绿 |
| 3 | `burn.py --board-a` | 烧录成功（DTR 复位）|
| 4 | 首配（无 bond、手柄快闪）| 连续 inquiry 扫到 → `candidate` → `open:` → 稳定 `[hid] state` |
| 5 | 冷启动有 bond | `outbound page bonded` → `open:` ≤ ~2s（对齐现状）|
| 6 | power-cycle reconnect | close → backoff → page → `open:` 快速恢复（≤ ~1s）|
| 7 | 手柄 re-pair（我们留旧 bond）| `open FAIL 0xffffffff` → `bond removed` → 下轮 inquiry 全新 Just Works |
| 8 | 无 `while(1)` | 源码 grep 无 `while (1)`/`esp_hid_scan`；空闲态 CPU 靠事件驱动、无 200ms 轮询节拍 |
| 9 | 行为等价 | 候选→open 触发仍为 `stable≥3s`/`total≥8s`（无新增抢先锁）；`[hid] candidate→connecting` 时延 ≤ 现状（连续 inquiry 逐事件更新应更快或持平）|
| 10 | 回归解码 | `[hid] state` 打印格式与重构前一致（解码未动）|

## 十、风险与决策

- **回调并发写共享态** → 单把 `g_app_mutex` 串行化候选/EWMA/state 访问；临界区不阻塞。
- **连续 inquiry 占射频** → 锁定/连接即 `cancel_discovery`；仅 SCANNING 空转时持续，可接受。
  inquiry `INQ_LEN`（`start_discovery` length 参数，单位 1.28s×N）取 ~8（≈10s），STOPPED 后续扫，
  兼顾发现率与重起开销。
- **EIR 名字时机**：首帧可能只有 COD 无名，名字随后续 `DISC_RES`/remote name 到达；
  逐事件处理天然覆盖（无名先跳过，名字到达即命中）。
- **esp_hidh 内部仍依赖 esp_hid_gap 头？** 否。`esp_hidh_*` 属 `esp_hid` 组件，与被删的本地
  `esp_hid_gap.c` 无关；`esp_hid_scan_result_t`/`esp_hid_usage_from_cod` 等来自 `esp_hid_common.h`
  （组件头），我们若不再用 `esp_hid_scan_result_t` 结构（改自定义 inq 结果解析），依赖更少。
- **回退策略**：本重构在独立分支，`bt-hid-report-decode` 合并后的轮询版在 main 可回退。
- **BLE 是否将来要用（open question）**：改键/宏/OTA 究竟走 BR/EDR HID vendor report 还是 BLE
  GATT **尚未确证**。当前决策=BR/EDR 流程事件化、BLE 射频保留但不接业务；后续做 Task 反格式/震动/
  OTA 时若发现需要 BLE，再按 §四「BLE 定位」补 `ble_gatt.{c,h}` 增量模块（不动本链）。

## 十一、完成定义

- `apps/default/` 无 `while(1)`、无 `esp_hid_scan`；连接/发现/重连全由 GAP/HID 回调 + `esp_timer`
  驱动；`esp_hid_gap.{c,h}` 删除，`bt_stack.{c,h}` 承接底层 init。
- 验证矩阵 4–9 全过；行为不回退（SSP 首配、bond 重连、stale-key 清键、close 回扫）。
- `main.c` 显著瘦身（扫描批处理块消失，改为逐事件函数）；文件分节顺序合规、全部格式化。
- `AGENTS.md`/`README` 同步该架构说明；提交在 `bt-hid-callback-refactor` 分支，**未合并**
  （等用户指令）。

## 十二、验证记录（2026-09-06 真机，host 起始 bonds=0）

回调链三场景全部纯事件/定时器驱动跑通，无轮询节拍：

| 场景 | 事件时间线（相对复位后 ms 前缀 `[+s]`）| 时延 |
|---|---|---|
| ① 全新首配 | `candidate +16.33` → `connecting +16.43` → `[gap] AUTH_CMPL … stat=0 OK lk=4 +18.71` → `[hid] open: +18.77` → `[hid] state:` | candidate→open ≈ **2.4s** |
| ② bonded 重连 | `[hid] close +33.4` → `outbound page bonded +33.7` → `[hid] open: +35.2` | page→open ≈ **1.5s** |
| ③ 手柄侧残留旧 bond | `outbound page +49` → `open FAIL 0xffffffff` → `bond removed` → `candidate +49.7` → `connecting` → `connect timeout +56.8` → `3 connect fails … dropping bond, re-pair if needed` → `bond_num=0 +79.25` | ~30s 内自动清键、回到可全新配对态 |

结论：行为与轮询版等价（首配快、bonded page 重连快、stale-key 自动清键、close→回扫），且 `app_main` 起步即返回、无 `while(1)`。③ 的卡住根因是 **bond 残留于手柄侧、主机不可远程清除**（BT 配对本质，非本次重构引入，固件已打印可执行提示）。`[hid] state` 全按钮/`lx=-32513` 为上一里程碑未实测校正的按钮位映射，本次未触碰解码。

## 十三、续：bonded-probe 启发式修复（让第 3+ 次重配对行为一致）

**问题**（用户实测，原 §十二 之外）：1、2 次重配对快，第 3+ 次卡住（手柄 "连接中" 永久不连）。
- 根因：板子有 bond 时先 `outbound page`，对手柄在"全新配对（可被发现）"分支会用旧加密请求 ACL，控制器产生**半死 ACL 槽**。v6.0.2 Bluedroid 没有公开的经典 BT disconnect API（`esp_bt_gap_disconnect` 已移除），唯一能拆 ACL 的 `esp_bt_hid_host_disconnect` 与 `esp_bt_gap_remove_bond_device` 互动在 v6.0.2 上触发了**重启循环**（实测，已回滚）。
- 结论：那条路不安全。改**不产生半死 ACL**——bond>0 时先做短探询。

**修法**：`resume_scan` 检测到 `bond_num > 0` 时，先发起一段 `INQ_LENGTH_BONDED = 2`（~2.56s）的 inquiry（不启动 lock_tick）。
- 若在这段 inquiry 里命中**与 bond[0] 同 BDA 且名字在白名单** → 手柄在全新配对（可被发现）→ 立刻 `remove_bond_of` + `dev_open` 走全新 SSP（不走 outbound page，无半死 ACL 风险），**清掉 ACL 槽再干净 Just Works**。
- 若 inquiry 自然结束未命中 → 手柄在 page-only bonded-reconnect 态 → 回退到 `open_bonded()` 走原 outbound page 快路径。

`g_probe` 标志 + `g_probe_bda` 缓存，全程在 `g_app_mutex` 内。`DISC_STATE=STOPPED` 在 probe 模式下触发回退；probe 命中路径 `issue_connect()` 内 `halt_scanning_side_effects()` 自动 cancel discovery + stop tick。

**真机验证**（commit `241277d`，120s 抓取）：7 次成功连接，0 死锁，0 重启。
| 路径 | 时延 | 备注 |
|---|---|---|
| 全新首配（无 bond） | ~3s | 不变 |
| bonded 回连（probe 探不到 → 回退 outbound page） | ~5.7s | +~2.5s 探询等待 |
| 探询命中 → 全新 SSP（**修复点**） | ~1.5s | 原 30s+ 死锁，现 ~1.5s |

用户视角：每次重配对行为一致（~1.5–6s 内连上，不再有"第 3 次卡死"）。代价仅 bonded 回连多 ~2.5s 探询。

## 十四、续：OPEN OK 清 `g_probe` + DISC_STOP 加 SCANNING 守卫（修"断电→上电"死锁）

**问题**（用户实测）：配对成功后**断电手柄→再上电**（手柄做 bonded-reconnect 主动 page 我们）时，手柄卡 "连接中"。

**根因**：手柄上电 page 我们时，我们的 probe 探询刚启动不久：
1. 控制器收页 → ACL 建立 → esp_hidh 投递 `OPEN EVENT (OK)`（入站连接）。
2. OPEN OK handler 里**漏清 `g_probe`**。
3. 我们自己 `cancel_discovery` 触发的 `DISC_STATE=STOPPED` 到了 → handler 看到 `g_probe==true` → 走"probe 回退" → `open_bonded()` 把 `g_state` 从 **CONNECTED** 改写为 **CONNECTING** + 又发一次 `dev_open` → 链接被打乱，手柄那边回不上 → "连接中" 卡死。

**修法**（两处都属 §十三 bonded-probe 引入的清理）：
1. OPEN OK 进锁后**清 `g_probe`**（在 `halt_scanning_side_effects()` 之后、`unlock()` 之前）。
2. DISC_STOP 的 probe 回退分支加 `g_state == ST_SCANNING` 守卫（即使 g_probe 漏了，CONNECTED 状态也不会被回退覆盖）—— 防御性冗余。

**真机验证**（待）：commit `3499bfe`，烧好后做"配对→断电→上电"，手柄应不再卡"连接中"，应在 ~2–6s 内连上（取决于手柄走 discoverable 还是 page-only 分支）。
