# default app 正式固件：连接与配对逻辑设计

日期：2026-08-20
状态：设计定稿
目标固件：`wireless/bs21/apps/default`（正式固件入口，app 名 `flydigi-wireless`）

## 1. 目标

把 `default` app 从"硬编码目标地址的扫描+连接状态机"改造成正式固件的
**连接管理 + 配对逻辑**，满足以下行为：

1. 没有连接过手柄时，总是进入搜索模式（扫描等"靠近"）。
2. 连接成功后，将设备地址记录到长期存储（NV）。
3. 有记录时，总是尝试连接记录的最后一个设备（自动重连）。
4. 长按 IO0 按键进入"重新配对模式"。
5. 配对模式下通过 **RSSI 信号强度阈值**判断手柄"靠近"并自动连接（接收器无屏幕，
   无设备选择 UI）。
6. 配对成功后清除旧记录、保存新记录；配对失败 / 短按退出 / 超时则保留旧记录，
   继续尝试连接旧设备。

## 2. 硬件资源映射

| 资源 | 引脚 | 说明 |
|------|------|------|
| 按键 IO0 | `S_MGPIO0`（管脚 16） | 按下拉低（内部上拉），`uapi_gpio_get_val()==0` 表示按下；轮询 + 消抖 |
| 红灯 | IO11（管脚 4） | **高电平点亮**；FATAL（NV 损坏）常亮 |
| 蓝灯 | IO13（管脚 6） | **高电平点亮**；状态提示（见下表） |
| NV 存储 | `uapi_nv_write/read` | key 落在 user normal 区 `[0x3000,0x4000)` |

> LED 为高电平点亮。IO12 绿灯暂不使用（预留）。
> 蓝灯做**状态转换提示**，红灯做**故障提示**。

LED 提示矩阵：

| 状态 | 蓝灯 IO13 | 红灯 IO11 |
|------|-----------|-----------|
| SEARCH | **快闪** 125ms | 灭 |
| RECONNECT | **慢闪** 1000ms | 灭 |
| ACTIVE | 熄灭 | 灭 |
| FATAL（NV 损坏） | 灭 | **常亮** |
> IO0 在 Ai-BS21-32S-Kit 上丝印标 "IO0"，对应 GPIO0（`GPIO0/XL1/SPI0_RXD/DMIC_DIN/EXTLNA_CTRL`），
> 规格书上的板载按键只有 Power 与 RST（复位），IO0 需外接按键。

## 3. 总体状态机

**FATAL 是全局门闩状态**：NV 读写损坏（重试 3 次仍失败）后进入，整个连接流程
停止、主循环阻塞，仅重启恢复。FATAL 体现在主状态机顶层，不在某函数内部判断，
保证代码可读。

```
FATAL（全局门闩）：NV 损坏 → 阻塞连接流程，红灯常亮，仅重启恢复
  └─ 触发后状态机不再执行任何连接/搜索动作

主状态机（FATAL 未触发时）：
BOOT
  ├─ 初始化（NV 初始化、GPIO/LED/按键配置）
  ├─ 读 NV 记录
  ├─ 有记录 ──────> RECONNECT（锁定记录地址 seek → 连接 → 配对[复用 SMP 密钥] → ACTIVE）
  └─ 无记录 ──────> SEARCH(无 timeout)（扫描，RSSI 靠近判定 → 连接+配对 → 存记录 → ACTIVE）

断开（disc:0x7 / 0x10）→ 统一回 RECONNECT（有记录）或 SEARCH(无 timeout)（无记录）
```

### 状态说明

- **RECONNECT**：读取 NV 里的记录地址，锁定该地址执行 seek → 连接 → 配对
  （SMP 密钥已存，直接复用）→ ACTIVE。蓝灯慢闪。
- **SEARCH（参数 `timeout`）**：统一搜索态，运行 RSSI 靠近判定（见 §5），锁定靠近
  设备后连接+配对。蓝灯快闪。
  - **无 timeout**：永久搜索。无记录 / 断开后无记录 / 长按但无记录时进入。配对成功
    **存**新记录（首次配对）。
  - **带 timeout**：`PAIR_TIMEOUT_MS`(2min) 限时。长按且**有旧记录**时进入（换新手柄）。
    配对成功**覆盖**旧记录。超时 → 有记录回 RECONNECT，无记录回 SEARCH(无 timeout)。
- **ACTIVE**：已连接。蓝灯熄灭。
- **FATAL**：全局门闩。NV 读写重试超限 → 阻塞连接流程，红灯常亮，仅重启恢复。

> SEARCH 与配对模式本质是同一搜索态，区别仅在是否带 timeout（超时）与是否覆盖记录，
> 故合并为单一 SEARCH 状态 + `timeout` 参数，不再设独立 PAIR 模式。

## 4. 按键逻辑

按键在独立后台任务轮询（含消抖），不阻塞连接流程。长按进入 SEARCH 的意图统一在
按键 callback 里判断是否有旧记录，决定 SEARCH 是否带 timeout。

| 事件 | 条件 | 动作 |
|------|------|------|
| 长按 | 按下持续 `LONG_PRESS_MS`(3000) | 判断 `record_valid`：有记录 → SEARCH(带 timeout，覆盖记录)；无记录 → SEARCH(无 timeout) |
| 短按 | 按下时间 < `LONG_PRESS_MS` 后松开 | 若 SEARCH 带 timeout → 退出配对，回 RECONNECT（有记录）/ SEARCH(无 timeout)（无记录）；否则无操作 |
| 无操作 | — | 无动作 |

- SEARCH 蓝灯快闪（125ms），RECONNECT 蓝灯慢闪（1000ms）。
- 进入 SEARCH 会停止当前连接动作（若正连旧设备则断开/停止 seek），切到扫描。

## 5. RSSI 靠近判定逻辑

这是配对模式 / 无记录搜索的核心选点逻辑，目标是从广播中选出"稳定靠近"的手柄。

### 5.1 信号滤波（抗单帧抖动）

单帧 RSSI 抖动大（±几 dB 常见）。为每个候选设备维护一个短滑动窗口
（`RSSI_FILTER_WIN`，默认 8 帧），对该设备最近若干帧 RSSI 取**平均值**作为
滤波后的有效信号 `rssi_f`。用 `rssi_f` 参与阈值判断，减少误判与抖动。

### 5.2 靠近判定（持续保持 + 更强抢占重置）

- **强信号**：某设备滤波后 `rssi_f >= -RSSI_THRESHOLD`（默认阈值 50，即 ≥ -50dBm）。
- 全局维护一个"当前靠近候选" `current_best`：
  - 首次某设备 `rssi_f` 达到强信号 → 设为候选，记录 `strong_since = now`（开始计时）。
  - 该候选持续保持强信号达到 `RSSI_HOLD_MS`(2000ms) → **锁定该设备**，停止 seek，
    发起连接。
- **更强设备抢占（带滞后）**：
  - 若出现新设备 `rssi_f` 比当前候选**强至少 `RSSI_SWITCH_DB`(3dB) 且保持
    `RSSI_SWITCH_HOLD_MS`(500ms)** → 切换候选为它，`strong_since = now`（重置计时）。
  - 滞后（hysteresis）避免两设备信号相近时来回横跳。
- **信号丢失**：
  - 当前候选若超过 `RSSI_LOST_MS`(1000ms) 未再收到强信号帧（走远/跌破阈值/广播停）
    → 视为靠近不成立，丢弃候选，重新等待。

### 5.3 可标定参数（宏）

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `RSSI_THRESHOLD` | 50 | 靠近判定阈值（绝对值，判断 `rssi_f >= -50`），用扫描固件实测标定 |
| `RSSI_FILTER_WIN` | 8 | 滑动平均窗口（帧数） |
| `RSSI_HOLD_MS` | 2000 | 靠近需持续保持的时长 |
| `RSSI_SWITCH_DB` | 3 | 更强者抢占所需的信号差 |
| `RSSI_SWITCH_HOLD_MS` | 500 | 更强者需保持的时长 |
| `RSSI_LOST_MS` | 1000 | 候选失联宽限 |
| `LONG_PRESS_MS` | 3000 | 长按进入 SEARCH(带 timeout) 的阈值 |
| `SEARCH_BLINK_MS` | 125 | SEARCH 蓝灯快闪间隔 |
| `RECONNECT_BLINK_MS` | 1000 | RECONNECT 蓝灯慢闪间隔 |
| `PAIR_TIMEOUT_MS` | 120000 | SEARCH(带 timeout) 限时（2 分钟） |

## 6. NV 长期存储

- 接口：`uapi_nv_write` / `uapi_nv_read`（key 用 user normal 区，如 `0x3001`）。
- 内容：连接记录的设备地址（6 字节）+ 有效标志（1 字节）。结构：

  ```c
  typedef struct {
      uint8_t valid;             /* 0xAA 表示有效记录 */
      uint8_t addr[SLE_ADDR_LEN]; /* 记录的手柄 SLE 地址 */
  } conn_record_t;
  ```

- **读写失败重试**：NV 读写失败时重试（`NV_RETRY_MAX`，默认 3 次）。
- **严重错误（FATAL）**：重试仍失败 = 严重错误 → 进入 **FATAL 全局门闩状态**，
  阻塞连接流程（停止搜索/连接），常亮 IO11 红灯，仅重启恢复。避免存储损坏导致
  反复重复搜索的死循环。
- SEARCH(带 timeout) 配对成功 → 用新设备地址覆盖记录；SEARCH(无 timeout) → 存新记录。

## 7. 数据流 / 错误处理

- **断连**（`disc:0x7` 链路超时 / `0x10` 远端断开）→ 统一回 RECONNECT（有记录）或
  SEARCH(无 timeout)（无记录）。
- **NV 读写失败** → 重试；多次失败 → 进入 FATAL 门闩（阻塞连接流程，红灯常亮）。
- **连接/配对失败** → 有限重试；SEARCH(带 timeout) 中超限回退 RECONNECT 连旧设备。
- **配对（SMP）失败**：与已有 `auth_complete_cb` 保存 SMP 密钥配合；密钥不对称时
  按旧设备重连流程处理（此前已验证密钥清除后需重新配对）。

## 8. 复用现有实现

`default/main.c` 已有可复用的部分：
- `seek_result_cb` / `seek_disable_cb` / `conn_state_changed_cb` / `pair_complete_cb`
  / `auth_complete_cb` / `conn_param_update_cb` 回调框架。
- param update（superv=200）与 SMP 密钥保存逻辑。
- `bs21_rst()`、`scan_start()`（seek 参数）。

需改造：
- 硬编码 `CONNECT_TARGET_ADDR` 锁定 → 改为 NV 记录地址 / RSSI 靠近选点。
- 增加：NV 读写、按键任务、LED 控制、RSSI 靠近判定、配对模式状态机。

## 9. 测试计划

1. **无记录首连**：清空 NV → 上电 → SEARCH(无 timeout) → 手柄靠近 → 连接+配对 →
   NV 写入记录 → 重启 → 自动连旧设备。SEARCH 蓝灯快闪。
2. **长按进入 SEARCH(带 timeout)**：长按 IO0（有记录）→ 蓝灯快闪 → 新手柄靠近 →
   配对成功 → 覆盖记录。
3. **短按退出**：长按进入 SEARCH(带 timeout) → 短按 → 退出 → 回连旧设备。
4. **SEARCH(带 timeout) 超时**：长按进入 → 无手柄靠近 → 2 分钟后回连旧设备。
5. **断连重连**：连接后断连 → 自动回 RECONNECT（蓝灯慢闪）重连。
6. **NV 失败（FATAL）**：模拟 NV 写入失败 → 重试 → 多次失败 → 进入 FATAL，
   阻塞连接流程、红灯常亮。
7. **RSSI 判定稳定性**：两设备靠近 → 选更强更稳者；信号抖动不误连。

## 10. 范围外（YAGNI）

- 不做设备选择 UI（无屏幕）。
- 不记录 RSSI / 使用次数等（当前只需"连最后一个设备"）。
- IO12 绿灯、其他 LED 效果暂不实现。
