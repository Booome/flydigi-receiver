# M6: SLE 连接尝试 —— 设计文档

日期：2026-08-18
状态：已批准（用户逐节确认）

## 背景

M5 已将 `default` app 改造为 SLE 扫描器（G 节点，聚合统计设备表），并锁定手柄
SLE 地址 `a1:a2:c8:75:43:b8`（开机持续广播、关机停止广播，RSSI -43~-49 dBm）。

M6 目标（见 `docs/bs21-development.md` M6 路线图）：基于 M5 获取的地址向手柄发起
SLE 连接，观察连接/配对各阶段状态；若配对被拒，分析拒绝原因。远期演进方向为
Xbox 式无线接收器（自动发现 → 自动连接 → 自动配对），因此 M6 采用显式状态机
架构作为后续演进骨架。

## 范围

- 只做"连接 + 配对"验证，不做数据收发（数据收发属 M7）
- 全自动触发 + 无限重试
- 连接参数全部用 SDK 默认值（不调 `sle_default_connection_param_set`）
- 日志输出走 USB2 串口（沿用 M5 的 `[scan]` 前缀风格，新增 `[conn]` 前缀）

## 状态机

```
SCAN ──锁定目标地址──▶ [停止 seek]
 [seek_disable_cb]      │
        ▼              ▼
   CONNECTING ──CONNECTED 回调──▶ PAIRING（若未配对）
        │  ▲                        │ pair_complete
        │  │                        ▼
        │  │                      ACTIVE（保持连接，观察稳定）
        │  │                        │ DISCONNECTED
        │  └──NONE/失败─────────────┘
        └────────▶ 回到 SCAN（重新扫描）
```

四个状态：

| 状态 | 含义 |
|---|---|
| `SCAN` | 正在扫描（seek 运行），聚合设备表 |
| `CONNECTING` | 已调用 `sle_connect_remote_device`，等待连接状态回调 |
| `PAIRING` | 已连接且未配对，已调用 `sle_pair_remote_device`，等待配对回调 |
| `ACTIVE` | 连接保持，观察稳定性（含配对已完成的连接） |

状态转换全部由回调驱动，回调运行于 SLE service 线程，只做内存状态修改 + 日志，
不阻塞、不长时间等待（延续 M5 spec §3 原则）。

## 回调映射

| 回调 | 触发时机 | 动作 |
|---|---|---|
| `sle_enable_cb` | SLE 就绪 | 注册连接回调 + 启动 seek，状态 → `SCAN` |
| `seek_result_cb` | 扫描到设备 | 地址匹配 `CONNECT_TARGET_ADDR` → 锁定 + `sle_stop_seek()`；否则仅入聚合表（沿用现有逻辑） |
| `seek_disable_cb` | 停止 seek 完成 | 状态 → `CONNECTING` → `sle_connect_remote_device(&目标)` |
| `connect_state_changed_cb` | 连接状态变化 | `CONNECTED` → 若 `pair_state == SLE_PAIR_NONE` 则状态 → `PAIRING` 并 `sle_pair_remote_device()`，否则直接 → `ACTIVE`；`NONE`/`DISCONNECTED` → 打印 `disc_reason` → 回 `SCAN` 重扫 |
| `pair_complete_cb` | 配对完成 | 打印 status（0 = 成功）；失败则记录原因但仍保持 `ACTIVE` 观察连接 |

## 目标地址

- 宏 `CONNECT_TARGET_ADDR`（6 字节数组），默认值 = 手柄地址
  `{0xa1, 0xa2, 0xc8, 0x75, 0x43, 0xb8}`
- `seek_result_cb` 中按 6 字节比对，命中即锁定（先 `sle_stop_seek()`）

## 错误处理与重试

- `sle_connect_remote_device` 返回非 0 → 打印错误码 → 回 `SCAN` 重扫
- `sle_pair_remote_device` 返回非 0 → 打印错误码 → 保持 `ACTIVE` 观察
- 断开（远端 `SLE_DISCONNECT_BY_REMOTE` 0x10 / 本端 `SLE_DISCONNECT_BY_LOCAL` 0x11）→
  打印原因 → 回 `SCAN` 无限重试
- 断链回 `SCAN` 前保留聚合表（计数累积），回扫后继续聚合
- 连接参数全用 SDK 默认（`sle_default_connection_param_set` 不调用）

## 日志格式

`[conn]` 前缀事件日志（USB2 串口，115200）：

```
[conn] target: a1:a2:c8:75:43:b8
[conn] target locked, stopping seek
[conn] connecting...
[conn] connected, pair_state:0x03
[conn] pairing...
[conn] paired: 0x0            （0 = 成功，非 0 = 拒绝原因错误码）
[conn] disconnected: 0x11     （0x10 = 远端断链，0x11 = 本端断链）
[conn] rescan...
```

## 测试方案

1. **编译验证**：`cmake -S wireless/bs21 -B wireless/bs21/build` +
   `cmake --build wireless/bs21/build -j` 无错误；fwpkg patch 表 TBL/CMP 均为 True
   （回归检查）
2. **板对板**：board_b 烧 M6 固件，`CONNECT_TARGET_ADDR` 临时指向 board_a 的广播
   地址（需把 board_a 的 `t_broadcaster` `main.c` 中全零广播地址改为非零 6 字节，
   否则全零地址无法作为连接目标）。验证连接流程走通或按失败路径正确回扫。
   - 若广播者不支持被 central 连接（纯广播不响应连接请求），板对板只验证到
     "锁定 → 连接 → NONE 回扫"的失败路径
3. **手柄验证**：`CONNECT_TARGET_ADDR` 改回手柄地址 `a1:a2:c8:75:43:b8`，
   开手柄（PC 模式）→ 观察是否 `CONNECTED` / `PAIRING`，以及断链时的远端/本端
   原因。若配对被拒，记录拒绝原因错误码并分析。

## 约束

- SDK（`~/.local/Ai-BS21_SDK`）只读引用，不修改
- 连接目标地址、状态机等逻辑全部在 `apps/default/main.c` 内演进
- 回调路径零打印以外的副作用（只做状态修改 + 日志）
- 代码注释英文；设计文档中文

## 成功标准

连接建立（即使配对失败）即为 M6 成功（roadmap M6 定义），并完整记录：
连接状态变化、配对结果、断链原因。