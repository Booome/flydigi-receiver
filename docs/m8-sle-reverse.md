# M8 Apex 5 直连 SLE 逆向分析记录

> 本文档记录 M8 阶段对飞智八爪鱼5 (Apex 5) 直连 SLE 的逆向过程与关键结论。
> 属于进度记录,供后续继续排障时快速回顾。

## 目标

用 BS21 直接作为 Apex 5 的 SLE 客户端接收其输入,替代官方 dongle。

## 已确立的硬事实

### SLE 链路层
- 控制器 = SLE server,地址 `a1:a2:c8:75:43:b8`,广播名 `fly_digig1` / `fly_digigs`
- 我们 BS21 = SLE client,配对成功 (SLE_PAIR_NONE → paired),MTU 520
- 本地地址 `aa:bb:cc:dd:ee:02`(项目约定,非 dongle 地址)

### SSAP handle/type 寻址语义(基于 Ai-BS21_SDK)
- `ssapc_read_req(client_id, conn_id, handle, type)` 与 `ssapc_write_req(client_id, conn_id, &{handle,type,data_len,data})` 是**成对接口**。
- `handle` 定位"哪个属性";`type` 定位"该属性的哪一部分"。
- `type` 取值(ssap_property_type_t):`0x00` 值、`0x02` CCCD、`0x03` 服务端配置、`0xFF` 厂商自定义等。
- **读/写应使用同样格式**:写入某 (handle,type) 的数据应与该结构定义的长度匹配,
  不应凭空拼超长帧。
- 发现的 `ssapc_find_property_result_t.descriptors_type[]` 列出每个 handle 下合法的 type 集合。

## 权威 handle/type 结构表(实验 N,两轮一致)

发现结果与逐结构体读取(响应按请求顺序,cfm[N] 序号对齐):

| handle | oper_ind | descriptor_types[] | (handle,type) 读取 | 长度 | 说明 |
|--------|----------|-------------------|--------------------|------|------|
| 0x11 | 781 (READ\|WRITE\|NOTIFY+0x100) | **[0x02]** | (0x11,0x00) 值 = `00 00 00 00` | **4B** | |
| 0x11 | 同上 | 同上 | **(0x11,0x02) CCCD = `02 00`** | **2B** | ⭐ =0x0002 指示模式 |
| 0x12 | 5 (READ\|WRITE_NO_RSP) | 无 | (0x12,0x00) = `01 01 11 00 00 00 00 00` | **8B** | |
| 0x13 | 13 (READ\|WRITE\|NOTIFY) | 无 | (0x13,0x00) = HID 鼠标报告描述符 | 69B | |
| 0x14 | 2 (WRITE_NO_RSP) | 无 | (0x14,0x00) = `06 00` | **2B** | |
| 0x16 | 1 (READ) | 无 | (0x16,0x00) = `fly_digigs` | 10B | DIS 名称 |
| 0x17 | 1 (READ) | 无 | (0x17,0x00) = `00 05 02` | 3B | |
| 0x18 | 1 (READ) | 无 | (0x18,0x00) = `MAGIC-103F-12D1-0001` | 20B | 序列号 |

### 关键结论
1. **只有 0x11 有描述符(0x02 CCCD)**,是唯一可"订阅"的属性 → 通知/指示通道。
2. **0x11 的 CCCD 预置 = `02 00`(0x0002 = 指示模式)** —— 手柄固件用**指示**
   (indication) 而非通知在 0x11 上推数据/响应。这对后续订阅方式有指导意义。
3. **除 0x11 外所有 handle 只有 type=0x00(值)**,无描述符。
4. 每个 (handle,type) 结构的长度**固定**(同一会话内读取稳定),handle+type 唯一确定一个结构体。
5. 跨会话 0x11 值会变(如 `01 00` vs `00 00 00 00`),反映手柄状态机。

## 已排除/纠错的假设

### ❌ `5a a5 f0 ee ...` 17 字节"长帧"写入(已放弃)
- 曾假设往 0x11 写 `5a a5 f0 ee <13B>` 是"RF 配对命令",观察到手柄进入"校准中"、
  或全零载荷导致"Xinput 连接中"(断开)。
- **现判定不合理**:`ssapc_write_req`/`ssapc_read_req` 应格式对称,0x11 值只有 4B,
  写 17B 超出结构长度,触发的是固件错误处理/越界,而非"命令被解析"。
- 此前从官方 XBOX 例程 (`fdg_fifo_interface.h`) 抄来的 `5a a5` 帧头/类型字节
  (0xEE/0xF0) 属于 **dongle↔PC 或私有 RF 层**,不一定是 SSAP 写值格式,参考价值存疑。

### 已排除的数据通道
| 通道 | 尝试 | 结果 |
|------|------|------|
| SSAP 通知 | CCC 使能 0x11/0x13(写 0x0001) | 零通知 |
| SSAP METHOD/EVENT | find type 0x04/0x05 | 不存在(结构查找被拒) |
| USB `5a a5` 命令 | get-info 等,带/不带 0x03 前缀 | 零 ACK(协议不对口) |
| SLE 低时延 | rx/dongle 模式,含启用 SDK 配置 | 控制器拒绝,立即断连 0x7 |
| SSAP 读轮询 | 反复读属性 | 值完全静态 |
| 控制点写入 | 向 0x12/0x14 写 0x01 | 不影响(未断连) |

### 绑定/单客户端
- **控制器绑定 dongle 后停止广播**(只对绑定地址做定向广播),第二个 SLE 客户端无法扫描到
- 控制器空闲时广播,BS21 可连接+配对,但**不推流**
- 与海思标准 RCU 实现一致:`sle_get_bonded_devices()` → 定向广播 → 只对绑定 dongle 连接

## dongle↔PC 观察
- 官方 dongle (`37d7:2501`) 是 BS21/BS20 芯片
- USB 接口:接口0 XInput 手柄(js0,subclass 0x5d)、接口1 键盘+鼠标(hidraw)、
  接口2 厂商 `5a a5` 通道(report 0x04 IN 31B)
- 手柄连 dongle 后 js0 有完整摇杆/扳机输入 → dongle→PC XInput 正常
- 厂商接口不携带实时输入,原始 SLE 数据隐藏在 dongle 内部

## 官方参考 (fbb_bs2x RCU 示例)
位置: `/home/bodong/.local/fbb_bs2x/src/application/samples/products/rcu/`
- `dongle/sle_rcu_dongle/sle_rcu_client.c` — 接收端(dongle)完整流程:
  扫描→匹配广播名→连接→配对→SSAP 发现→**`sle_update_connect_param`
  (interval=0x64, max_latency=3, supervision=0x1F4)**→收通知
- 参考客户端**不写 CCC**,靠服务器主动推
- 参考服务器:报告特征 UUID `37BEA880-FC70-11EA-B720-00000000103C` + CCCD

## 固件分析 (K5_MH2113_V7045_0409_DFU.bin)
- 控制器主固件,ARM Cortex-M,含 EasyFlash 键值存储
- ENV 键:`fdg_cfg_index`、`fdg_cfg_random_0..8`、`fdg_rgb_config_0..8`、
  `fdg_buttons_config_0..8`、`fdg_auto_trigger_*`、`fdg_global_datasave`,
  及扳机校准键 `tri_lt/rt_adc_value`、`tri_lt/rt_motor_value`、`tri_adc_min/max`、`tri_lrt_map`
- 未发现 `5a a5` 字节 → SLE/SSAP 协议逻辑可能在另一核(BS20)固件
- fwpkg 固件包经 AES-128-CBC 加密,密钥由 ePass2001 硬件加密狗保护,无法软件破解

## 当前固件状态 (sle_probe, board_a)
- 实验开关:EXP_ENABLE_CCC / CTRL / PARAMUPD / LL / PAIRFRM / READS / SINGLE_PROBE
- 最新实验 N:逐 handle 遍历所有 type 读取,得出权威结构表

## 待验证 / 下一步
- **按结构长度写入**：对 0x12(8B)、0x14(2B)写长度匹配的值,观察行为/状态变化
- **订阅 0x11 指示(0x0002)**:保持手柄指示模式,配合写入,尝试捕获手柄主动推来的数据
  (校准进度/输入流/命令响应)
- 若仍零数据,转向"地址/绑定门控"或固件提取

## OS 抽象层备注

BS21 SDK 存在两套 OS 抽象接口，风格不统一：

| 接口 | 来源 | 用途 |
|------|------|------|
| `osal_kthread_create` / `osal_timer_init` | 海思自研 OSAL (LiteOS) | 线程创建、定时器 |
| `osTimerNew` / `osThreadNew` | CMSIS-RTOS2 标准 | 同上 |

**后续 default app 中多个定时器使用的是 `osTimerNew`(CMSIS-RTOS2)**，而非 `osal_timer_init`。
迁移或重构时需注意两套 API 不可混用，统一选型待定。

## 重大发现：`ssapc_find_structure` 的 type 白名单限制

### 现象

`find_structure` 按 type 查询时：
- **type=0 (SERVICE_STRUCTURE)**: 请求发出，对端返回 status=0x2（拒绝）
- **type=1 (PRIMARY_SERVICE)**: 正常返回 2 个服务
- **type=3 (PROPERTY)**: 正常返回 7 个属性
- **type=2 (REFERENCE_SERVICE) / 4 (METHOD) / 5 (EVENT)**: 永远等不到 `find_structure_cmp_cb`，表现为"卡死"

### 根因（反汇编确认）

反汇编 `libbth_gle.a` 中 `ssapc_find_structure` 的入口校验：

```asm
22:  lbu   a2,0(s0)        # a2 = param->type
24:  bgeui a2,4,5e         # type >= 4 → 拒绝
28:  li    a5,1
2a:  sll   a5,a5,a2        # a5 = 1 << type
2e:  andi  a5,a5,11        # a5 &= 0b1011   ← 位掩码白名单
30:  beqz  a5,5e           # == 0 → 拒绝
...
5e:  (错误分支) li a0,7     # 同步返回 errcode = 7，请求不发空中
```

位掩码 `0b1011` 白名单：

| type | 值 | 结果 |
|------|-----|------|
| 0 SERVICE_STRUCTURE | `0b0001 & 0b1011` | ✅ 放行 |
| 1 PRIMARY_SERVICE | `0b0010 & 0b1011` | ✅ 放行 |
| **2 REFERENCE_SERVICE** | `0b0100 & 0b1011 = 0` | ❌ **API 层直接拒绝** |
| 3 PROPERTY | `0b1000 & 0b1011` | ✅ 放行 |
| **4 METHOD / 5 EVENT** | — | ❌ `type>=4` 直接拒绝 |

**结论**：BS21 SDK 的 SSAP client API 只支持 find type 0/1/3。type=2/4/5 在 API 入口处被拒绝，同步返回 errcode=7，请求根本不会发到空中。手柄从未收到过这些请求。

### 我们的 bug

`start_next_find()` 没有检查 `ssapc_find_structure()` 的返回值。API 返回错误（errcode=7），我们忽略了它，继续傻等永远不会来的 `cmp_cb`——这就是"卡死"的真相。

### 修复

重构为**回调驱动 + 返回值检查**模式：

- 每个回调负责驱动下一步（发起下一个请求）
- 检查所有 SSAP/SLE API 的返回值
- 同步失败（API 返回非 SUCC）时，手动调用完成回调以推进状态机（相当于本地 NACK），避免卡死
- 全链路由回调自然驱动，断连回调立即触发 rescan，响应及时

核心修复在 `start_next_find()`：使用 while 循环连续跳过被 SDK 拒绝的类型（type 2/4/5），打印 REJECTED 后自动推进到下一个类型，最终完成发现流程。

### 返回值检查清单

| API | 返回值 | 处理 |
|-----|--------|------|
| `sle_scan_start()` | void | 无法检查（SDK 限制），靠 seek_disable_cb |
| `sle_stop_seek()` | errcode_t | 失败打印 |
| `sle_connect_remote_device()` | errcode_t | 失败→模拟断连回调触发 rescan |
| `sle_pair_remote_device()` | errcode_t | 失败→模拟 pair_complete_cb(错误)→rescan |
| `ssapc_exchange_info_req()` | errcode_t | 失败→模拟 exchange_info_cb(错误)→rescan |
| `ssapc_find_structure()` | errcode_t | 失败=SDK 不支持，打印 + 跳过 |
| `ssapc_read_req()` | errcode_t | 失败打印 |
| `ssapc_write_req()` | errcode_t | 失败打印 |
| `enable_sle()` | errcode_t | 失败→模拟 sle_enable_cb(错误)→重试 |
| `sle_remove_paired_remote_device()` | errcode_t | 失败打印（低风险） |

## 架构：回调驱动 + 返回值检查

### 旧架构（纯回调驱动，无返回值检查）

```
enable_sle() → cb → start_scan() → cb → connect() → cb → pair()
→ cb → exchange_info_req() → cb → find_structure() → cb → ...
```

每一步都依赖上一步的回调来推进。同步失败（API 返回非 SUCC）意味着回调永远不来，链条断裂卡死。

### 新架构（回调驱动 + 返回值参与流程控制）

```
enable_sle() ──失败──> 手动调用 sle_enable_cb(错误) ──重试
    │
  成功 cb
    ↓
start_scan() → seek_disable_cb → connect()
    │                              │
    │                            失败 → 手动调用 connect_state_changed_cb(DISCONNECTED) → rescan
    │                              │
    │                           成功 cb → pair()
    │                                     │
    │                                   失败 → 手动调用 pair_complete_cb(错误) → rescan
    │                                     │
    │                                  成功 cb → exchange_info_req()
    │                                             │
    │                                           失败 → 手动调用 exchange_info_cb(错误) → rescan
    │                                             │
    │                                          成功 cb → find_structure() 循环
    │                                                     │
    │                                                   被拒绝 → while 循环跳过，打印 REJECTED
    │                                                     │
    │                                                  成功 → cmp_cb 推进
    └─────────────────────────────────────────────────────┘
```

**核心设计**：
- 回调驱动链条推进（响应及时，断连立即触发 rescan）
- 所有 API 返回值参与流程控制
- 同步失败 = 手动调用完成回调（本地 NACK），链条不断
- `start_next_find()` 用 while 循环跳过 SDK 不支持的类型

**优点**：
- 断连响应及时（回调直接触发 rescan，无需等待 task 唤醒）
- 无卡死点（同步失败有明确的续链路径）
- 代码量小，逻辑集中

### 返回值检查清单

| API | 返回值 | 处理 |
|-----|--------|------|
| `sle_scan_start()` | void | 无法检查（SDK 限制），靠 seek_disable_cb |
| `sle_stop_seek()` | errcode_t | 失败打印 |
| `sle_connect_remote_device()` | errcode_t | 失败 retry |
| `sle_pair_remote_device()` | errcode_t | 失败 retry |
| `ssapc_exchange_info_req()` | errcode_t | 失败 retry |
| `ssapc_find_structure()` | errcode_t | 失败 = SDK 不支持，打印 + 跳过 |
| `ssapc_read_req()` | errcode_t | 失败打印 |
| `ssapc_write_req()` | errcode_t | 失败打印 |
| `enable_sle()` | errcode_t | 失败打印 |
| `sle_remove_paired_remote_device()` | errcode_t | 失败打印（低风险） |