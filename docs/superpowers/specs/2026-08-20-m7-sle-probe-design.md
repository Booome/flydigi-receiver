# M7：SLE 数据通道探索——sle_probe 被动接收探测 app

日期：2026-08-20
状态：设计定稿
目标：探索飞智八爪鱼5 手柄在 SLE 链路上的数据通道与 payload 格式

## 1. 目标

M7 探索阶段的核心问题是：**手柄连上接收器后，能否被动收到输入数据？
收到的 payload 长什么样？**

在确认"能否收到数据"之前不涉及解码与命令下发，因此本阶段交付一个最小
独立探测 app `sle_probe`：

1. 上电后扫描 SLE 设备，打印所有发现结果（地址 + 广播数据）。
2. 选 RSSI 最强的一台设备连接 + 配对。
3. 建立 SSAP client 通道，**被动接收**手柄推送的数据并 **hex 打印**。
4. 连接/配对/SSAP 各阶段状态码全部打印，便于观察握手细节。

## 2. 范围

### 范围内

- 独立探测 app `sle_probe`（基于 SDK `sle_uart_client` 示例最小改造）。
- 公共代码目录整理：`common/` 重命名 `src/`。
- 探测 app 复用 `src/scan_table` 做扫描收集。
- 被动接收 + hex 打印，不改动 default 正式固件。

### 范围外（本阶段 YAGNI）

- **不发送任何数据**（不发 NewXInput 初始化命令、不回震动）。
- **不解码** payload（不解 20B/32B 报告，不填 `controller_state`）。
- 不实现 RSSI 靠近判定、NV 记录、按键交互（default 已覆盖这些）。
- 不修改 default / 其他现有 app 的行为逻辑。
- 不在 SDK 源码内做任何改动（保持只读引用模式）。

## 3. 前置重构：`common/` → `src/`

公共代码从 `wireless/bs21/common/` 迁移到 `wireless/bs21/src/`，语义从
"per-app common"改为"全工程共享源码"。

### 3.1 内容与归属

| 文件 | 归属 | 说明 |
|------|------|------|
| `bs21_util.c/.h` | `src/` | 全 app 共用工具（6 个 app 已引用） |
| `scan_table.c/.h` | `src/` | 扫描收集表（addr+RSSI+count），sle_probe 复用 |
| `controller_state.h` | `src/` | 控制器状态结构（M7 后续解码用，暂无人引用） |

`apps/default/rssi_pick.c/.h` 留在 default（含 threshold/锁等 default 专属逻辑，
不公共化）。

### 3.2 操作

- `git mv common src`
- 更新 6 个 app 的 `CMakeLists.txt`：源码路径与 include path 中 `common/` → `src/`
- 代码内 include 均为路径无关引用（`<bs21_util.h>` 等），只改 include path，
  源码零改动
- 编译验证：`BS21_APP=default` 编译通过，行为不变

## 4. `sle_probe` 架构与组件

目录：`wireless/bs21/apps/sle_probe/`

```
apps/sle_probe/
├── main.c                 # APP_INIT 入口：SLE 初始化 + 回调注册 + 启动扫描
├── sle_probe_client.c     # 扫描/连接/配对/SSAP 逻辑（基于 sle_uart_client 改造）
├── sle_probe_client.h
└── CMakeLists.txt         # 复用 src/scan_table、src/bs21_util
```

构建方式：`BS21_APP=sle_probe cmake ...`（与现有 app 相同，`build/<app>/` 输出）。

### 4.1 组件职责

| 组件 | 职责 |
|------|------|
| `main.c` | SLE 初始化，注册 power/enable/seek/connect/SSAP 回调，启动扫描任务 |
| `sle_probe_client.c` | seek 收集（scan_table）、设备选择（RSSI 最强）、连接、配对、SSAP find structure、数据打印 |
| `src/scan_table` | 扫描结果收集（复用，不复制） |

## 5. 数据流

```
上电
 └─ SLE power on → enable → sle_start_seek()
      └─ seek 回调：每个发现设备 → 打印 [addr + 广播 data hex] → 写入 scan_table
           └─ 扫描持续 PROBE_SCAN_MS(5000)ms
                └─ 遍历 scan_table 选 RSSI 最强设备
                     └─ sle_connect_remote_device()
                          ├─ 连接成功 → sle_pair_remote_device()
                          │    ├─ 配对完成 → ssapc_register_client()
                          │    │    └─ ssapc_exchange_info_req()（MTU 交换）
                          │    │         └─ ssapc_find_structure()（发现服务/属性，打印）
                          │    │              └─ 进入监听：等 notification/indication
                          │    │                   └─ 收到数据 → [probe] recv len=NN + hex
                          │    └─ 配对失败 → 打印状态码 → 重新 seek
                          └─ 连接失败 → 打印状态码 → 重新 seek
断开（任意时机）
 └─ 打印断开状态码 → 重新 seek
```

## 6. seek 策略与设备选择

- **不按名称过滤**（手柄广播名未知，示例的 `strstr("sle_uart_server")` 移除）。
- seek 回调打印每个设备地址 + 广播数据 hex，并写入 `scan_table`。
- 扫描持续 `PROBE_SCAN_MS`(5000ms) 后停止 seek。
- 选 **RSSI 最强**设备连接：遍历 `scan_table` 取 `rssi` 最大项
  （`scan_table_print()` 在停止扫描时打印候选摘要）。

> 若 5s 内未发现任何设备，打印提示并继续 seek（循环等待），不自动退出。

## 7. SSAP 发现与数据接收

### 7.1 服务发现

配对完成后执行 `ssapc_find_structure`，把找到的 service/property
（handle、uuid、property 权限）逐条打印。这是探索手柄 SSAP 结构的直接手段。

### 7.2 数据接收

- `notification_cb` / `indication_cb`（`ssapc_handle_value_t *data`）：
  打印 `len = data->data_len` + 每字节 hex（一行一帧，便于对照 20B/32B 报告）。
- 示例代码将 UART 写回的部分删除（只打印不转发）。
- 是否需先发 enable/订阅由探索结果决定（SDK 的 `ssaps_notify_indicate` 为服务端
  主动推送，client 侧仅注册回调；若实测收不到数据，再补充属性订阅步骤）。

## 8. 日志格式

统一前缀 `[probe]`，关键节点：

```
[probe] power on: <status>
[probe] sle enable: <status>
[probe] seek result: addr=<XX:XX:..> rssi=<dB> data=<hex>
[probe] scan stop, <N> devices found
[probe] pick best: addr=<..> rssi=<dB>
[probe] connect: ret=<status>
[probe] connected, conn_id=<id>
[probe] pair: ret=<status>
[probe] pair state: <state>
[probe] ssap find: service handle=0x<..> uuid=...
[probe] ssap find: property handle=0x<..> uuid=...
[probe] recv len=<NN> <hex bytes>
[probe] disconnected, reason=0x<..>
[probe] error: <context> status=0x<..>
```

## 9. 错误处理

| 场景 | 处理 |
|------|------|
| seek 无设备 | 打印提示，继续 seek（循环） |
| 连接失败 | 打印状态码，等待后重新 seek |
| 配对失败 | 打印状态码，等待后重新 seek |
| SSAP find 失败 | 打印状态码，重新 find（限次） |
| 断开 | 打印 reason，重新 seek |

保持简单：无 NV、无状态机，错误一律"打印 + 回扫"。

## 10. 测试计划（板上）

前置：先在 `wireless/bs21` 主工作区完成 `common`→`src` 重构并验证 default 编译。

1. `BS21_APP=sle_probe` 编译通过（0 warning，-Werror）。
2. 烧录 board_a，上电观察：SLE enable、seek 开始。
3. 手柄切 2.4G（PC）模式开机，靠近 board_a：
   - 观察 seek 结果是否出现手柄（广播地址 + 数据内容）。
   - 观察是否选中最强、连接 + 配对成功。
   - 观察 `ssap find` 打印的手柄 SSAP 服务/属性结构。
   - 观察 `recv` 打印：是否收到数据帧、长度与内容规律。
   - 摇动摇杆 / 按键，观察数据是否变化。
4. 全程抓 reset 起完整 log 保存，供 payload 格式分析。
5. 记录每次失败的状态码，回传分析。

## 11. 成功标准

本阶段（被动接收）成功：**在串口 log 中观察到手柄推送的数据帧**
（`recv len=NN ...` 持续出现），即证明 SLE 数据通道打通。

## 12. 风险与未知

| 风险 | 说明 | 应对 |
|------|------|------|
| 手柄不推送数据 | 手柄可能需先收到特定命令才回数据（被动接收可能收不到） | 若探索发现无数据，转下一阶段尝试发送初始化命令（默认不发送） |
| 手柄 SSAP 结构未知 | 需 `find_structure` 实际发现 | 打印全部发现结果，不预设结构 |
| 手柄广播不包含设备名 | seek 按地址识别即可 | 打印全部设备 + 信号最强选择 |
| 配对行为差异 | 手柄可能与接收器采用不同 pairing 流程 | 打印 pair state/状态码，参照 default 经验 |

## 13. 后续方向（本阶段不做）

- 若数据通道打通：分析 payload 格式，对照 20B/32B USB 报告 → 解码到
  `controller_state`。
- 若需主动初始化：尝试发送 NewXInput 命令序列观察响应。
- 探索结论稳定后：将数据接收/解码逻辑迁入 default 正式固件。