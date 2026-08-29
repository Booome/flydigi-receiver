# SLE/SSAP 协议分析发现

## 1. SLE 帧格式

### 1.1 0x16 帧结构

所有 SSAP 通信都使用 **0x16 帧**（SLE 数据帧）：

```
┌──────────────────────────────────────────────────────────┐
│  SLE 帧 (0x16)                                           │
│  ┌────────────────────┬──────────────────────────────────┐│
│  │  SLE 帧头 (20字节)  │  SSAP PDU (可变长)                ││
│  │  [类型|连接ID|...]  │  [消息码|控制|数据]               ││
│  └────────────────────┴──────────────────────────────────┘│
└──────────────────────────────────────────────────────────┘
```

SLE 帧头示例（20 字节）：
```
16 00 10 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00
```

### 1.2 验证

- probe ↔ decoy：使用 0x16 帧
- dongle ↔ decoy：使用 0x16 帧
- 0x16 是 SLE 标准的数据帧类型，承载所有 SSAP PDU

## 2. SSAP PDU 类型

### 2.1 消息码定义

| 消息码 | 名称 | 方向 |
|--------|------|------|
| 0x02 | SSAP_EXCHANGE_INFO_REQ | Client → Server |
| 0x03 | SSAP_EXCHANGE_INFO_RSP | Server → Client |
| 0x04 | SSAP_FIND_STRUCTURE_REQ | Client → Server |
| 0x05 | SSAP_FIND_STRUCTURE_RSP | Server → Client |
| 0x06 | SSAP_FIND_STRUCTURE_BY_UUID_REQ | Client → Server |
| 0x07 | SSAP_FIND_STRUCTURE_BY_UUID_RSP | Server → Client |
| 0x08 | SSAP_READ_REQ | Client → Server |
| 0x09 | SSAP_READ_RSP | Server → Client |
| 0x0a | SSAP_READ_BY_UUID_REQ | Client → Server |
| 0x0b | SSAP_READ_BY_UUID_RSP | Server → Client |

### 2.2 Exchange Info PDU 结构

```
  0               1               2               3
  0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  |   0x02/0x03   |     ctrl      |              MTU              |
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  !           VERSION             |
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
```

- ctrl bit 0: 是否携带 MTU
- ctrl bit 1: 是否携带 version
- MTU: 默认 300 (0x012c)
- Version: 0x0101 (1.1)

### 2.3 Find Structure Request PDU 结构

```
  0               1               2               3
  0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  |     0x04      |     ctrl      |        start handle           |
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  !          end handle           |       uuid(length 2/16)...
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

ctrl 字段：
- bit 0-2: findType (0=服务结构, 1=首要服务, 2=引用服务, 3=属性, 4=方法, 5=事件)
- bit 3-4: itemType (0=标准, 1=厂商自定义, 2=混合)
- bit 5: rspMode (0=单次响应, 1=多次响应)

### 2.4 Find Structure Response PDU 结构

```
  0               1               2               3
  0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  |     0x05      |     ctrl      |        data...
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

## 3. Hook 点分析

### 3.1 已验证的 Hook

| Hook 函数 | 触发？ | 说明 |
|-----------|--------|------|
| `gle_tm_data_recv_core` | ✅ | 接收数据（结构体，非 PDU） |
| `gle_tm_data_send_core` | ✅ | 发送数据（结构体） |
| `cs_pdu_tl_send` | ✅ | 发送 0x16 帧（完整 48 字节） |
| `gle_ssaps_send_response` | ✅ | SSAP 层响应（probe 通信时） |

### 3.2 未触发的 Hook

| Hook 函数 | 触发？ | 说明 |
|-----------|--------|------|
| `ssaps_exchange_info_req_handle` | ❌ | Exchange Info 请求 |
| `ssaps_find_hdl_by_uuid_handle` | ❌ | Find Structure 请求 |
| `ssaps_read_req_handle` | ❌ | Read 请求 |
| `ssaps_event_callback_handler` | ❌ | SSAP 事件 |
| `btsrv_handle_ssaps_msg` | ❌ | SSAP 消息分发 |

**结论**：dongle 的请求由 BS21 ROM 固件直接处理，不经过主机栈的 SSAP 服务器函数。

### 3.3 结构体指针分析

`gle_tm_data_recv_core` 的 a0 参数是 28 字节结构体：

```
偏移  内容
0-3   指针 p0 → 连接上下文（含 MAC 地址）
4-7   指针 p1
8-11  指针 p2 → 可能包含 PDU
12-15 指针 p3 (flags/length)
16-19 指针 p4
20-23 指针 p5
24-27 指针 p6
```

p0 数据中的指针：
- p0[0]: 指向某数据结构
- p0[1]: 指向某数据结构
- p0[2]: **以 0x04 开头** → SSAP_FIND_STRUCTURE_REQ
- p0[3]: 指向某数据结构

## 4. Dongle 通信流程

### 4.1 观察到的模式

1. **第一次连接**：pair 失败（status=0x8000600d），断开
2. **后续连接**：pair 成功（status=0x0），但很快断开
3. **每次连接都发送 Exchange Info Response**

### 4.2 RX/TX 对应关系

| RX (dongle→decoy) | TX (decoy→dongle) |
|-------------------|-------------------|
| p0[2]-> 以 `04` 开头 (FIND_STRUCTURE_REQ) | `03 03 2c 01 01 01 05 12 30 8f ...` |

TX 帧格式：
- 前 6 字节：Exchange Info Response（`03 03 2c 01 01 01`）
- 第 7 字节起：实际响应（如 `05` Find Structure Response）

### 4.3 问题

TX 帧 payload 包含多个 PDU 或附加数据，格式不符合预期。可能原因：
1. ROM 固件在 Exchange Info Response 后附加了额外的响应数据
2. SLE 帧 payload 包含多个 PDU
3. 响应格式需要进一步解析

## 5. 开源参考

### 5.1 OpenHarmony Nearlink Service

- 仓库：https://gitcode.com/openharmony/communication_nearlink_service
- 本地路径：`~/workspace/communication_nearlink_service`
- 内容：SSAP 协议实现、SLE 广播/扫描/连接管理

### 5.2 关键发现

- SLE 标准 base UUID：`{0x37, 0xBE, 0xA8, 0x80, 0xFC, 0x70, 0x11, 0xEA, 0xB7, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}`
- SDK 的 `ssapc_find_structure_cbk` 返回的 UUID 前两个字节是 `37BE`（base UUID 前缀），这是 SDK 的 bug

## 6. Probe vs Dongle 对比

### 6.1 请求对比

| 参数 | Probe | Dongle |
|------|-------|--------|
| MTU | 300 (0x012c) | 300 (0x012c) |
| Version | 0x0101 (1.1) | 0x0101 (1.1) |
| findType | 0 (SERVICE_STRUCTURE) | 0 (SERVICE_STRUCTURE) |
| startHandle | 0 | 0 |
| endHandle | 0 | 0 |

**结论**：请求参数完全相同。

### 6.2 响应对比

| 字节 | Probe | Dongle |
|------|-------|--------|
| 0-5 (Exchange Info RSP) | `03 03 2c 01 01 01` | `03 03 2c 01 01 01` |
| 6-7 | `06 00` | `06 00` |
| 8+ | `d4 95 00 20 ...` (不同) | `88 8f 00 20 ...` (不同) |

**问题**：前 8 字节相同，但后续数据不同。

### 6.3 根因分析

1. **ROM 固件处理请求**：dongle 的请求由 ROM 固件直接处理，不经过主机栈的 SSAP 服务器函数
2. **动态响应数据**：ROM 固件生成的响应包含 RAM 指针等动态数据，每次运行都不同
3. **ssaps_send_response 未触发**：主机栈的 `gle_ssaps_send_response` hook 没有触发

### 6.4 关键发现

- **0x16 帧是标准 SLE 数据帧**，所有 SSAP 通信都使用 0x16 帧
- **ROM 固件生成响应**，而不是主机栈
- **响应包含动态数据**（RAM 指针），每次运行都不同
- **需要 hook ROM 固件或 DLI 层** 才能捕获完整的请求/响应

## 7. DLI 层 Hook 尝试

### 7.1 尝试的函数

- `hci_gle_rx_acb_data` - HCI GLE 接收 ACB 数据
- `hci_gle_rx_icb_data` - HCI GLE 接收 ICB 数据

### 7.2 结果

- 函数在 SDK 库中存在（`libbgtp.a`）
- 但未被链接进应用 binary
- `__wrap_*` 机制无法 hook 未链接的函数

### 7.3 原因

- DLI 函数由 ROM 固件或底层栈调用
- 应用层不直接调用这些函数
- 需要通过其他方式捕获数据

## 8. 开源代码分析

### 8.1 数据流架构

```
芯片 ROM 固件
    ↓
DLI (Data Link Interface)
    ↓ dliPacketReceived 回调
DTAP (Data Transport Adaptation Protocol)
    ↓ DTAP_DataRecv
TM (Transport Manager)
    ↓ gle_tm_data_recv_core
SSAP Server/Client
    ↓
应用层
```

### 8.2 DLI 数据包格式

```c
#define DLI_HEADER 5 /* data type (1), Handle (2), DLI Payload len (2) */
```

```
| type (1) | handle (2) | payload len (2) | payload (variable) |
```

DLI 数据包类型：
```c
typedef enum {
    PACKET_TYPE_SLE_CMD = 0xA1,
    PACKET_TYPE_SLE_EVENT = 0xA2,
    PACKET_TYPE_SLE_ACB = 0xA3,
    PACKET_TYPE_SLE_ICB = 0xA4
} SlePacketType;
```

### 8.3 SlePacket 结构体

```c
typedef struct SlePacket {
    uint8_t *data;
    uint32_t size;
} SlePacket;
```

### 8.4 Find Structure Response 格式

根据开源代码 `BuildPrimaryServiceInfo`：

**V1.1+ 格式**：
```
| startHandle (2) | endHandle (2) | uuid (2/16) | memberValue (1) |
```

**V1.0 格式**：
```
| startHandle (2) | endHandle (2) | memberValue (1) |
```

### 8.5 关键发现

1. **DLI 层是芯片与主机的接口**，数据通过回调函数 `dliPacketReceived` 传递
2. **DTAP 层负责数据重组**，将分片的数据组合成完整的 PDU
3. **TM 层负责数据传输**，`gle_tm_data_recv_core` 是主机接收数据的入口
4. **响应数据包含动态内容**（RAM 指针），每次运行都不同

## 9. 开放问题

1. **RX PDU 完整解析**：p0[2]-> 的数据格式尚未完全解码
2. **TX 帧格式**：为什么 payload 以 Exchange Info Response 开头？
3. **DLI 层 Hook**：需要其他方式捕获 DLI 层数据（如 JTAG、逻辑分析仪）
4. **ROM 固件行为**：ROM 固件如何处理请求并生成响应？
5. **响应一致性**：响应包含动态数据（RAM 指针），每次运行都不同

## 10. 下一步

1. 解码 p0[2]-> 的完整 PDU 结构
2. 分析 TX 帧中 Exchange Info Response 后的数据格式
3. 对比真实控制器的响应格式
4. 研究 ROM 固件的响应生成逻辑
5. 考虑使用 JTAG 或逻辑分析仪捕获芯片级数据
