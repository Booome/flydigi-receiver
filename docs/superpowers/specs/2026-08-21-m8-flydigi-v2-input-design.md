# M8 飞智 V2 协议激活 SLE 输入流 — 设计文档

> **日期**：2026-08-21
> **里程碑**：M8（飞智私有协议逆向 — 激活输入流）
> **状态**：设计已确认，进入实施

## 1. 背景与问题

M7 已验证 BS21(SLE central) 能与飞智 Apex 5 手柄（SLE peripheral，地址
`a1:a2:c8:75:43:b8`）完成：扫描→连接→配对→MTU 交换→SSAP 服务发现
（handles `0x11`–`0x18`，UUID 全为 `0xBE37` 私有服务）→ 读元数据
（"fly_digigs" / "MAGIC-103F-12D1-0001" / 状态字节）。

**阻塞点**：开启通知 + 发送裸字节 `0x01/0x03/0x05/0x08/0x02` 后，手柄持续
移动摇杆/按键仍 **0 条 notification/indication**。

### M7 失败的两个叠加根因（已定位）

1. **通知开启写错 handle**：M7 向 property value handle `0x12`–`0x19` 写
   `0x0001`（type=VALUE）。但 `0x12` 本身是下一个 property，并非 `0x11` 的
   CCC 描述符。正确做法：先 `find_descriptor` 拿到各 notify property 的 CCC
   描述符 handle，向其写 `0x0001`（type=DESCRIPTOR）。
2. **从未发送飞智 enable 命令**：M7 只发了裸单字节，而飞智 V2 协议需先发
   init 握手 + `enable` 命令才进入 extended 模式并流式输出输入报告。

## 2. 网上飞智逆向代码调研结论

参考 `docs/reference/flydigi/`（来自 padctl / flydigi-vader5 开源项目）：

飞智存在**两代协议**：

| | V1（Apex 4 DInput / 2.4G） | V2（Vader 5 Pro） |
|---|---|---|
| 输入报告头 | `04 FE 66`（32B） | **`5a a5 ef`**（32B） |
| VID:PID（接收器） | `04b4:2412` | `37d7:2401` |
| 激活方式 | DInput 模式自动流 | **需 init 握手 + enable 命令** |
| 命令帧格式 | — | `5a a5 <cmdId> <len> <payload> <chk>` |

Apex 5（2025/2026 最新）与 Vader 5 Pro 同代，**几乎肯定用 V2**。Vader 5 的
确切 init + enable 字节（来自 `vader5.toml`）：

```
init:   5a a5 01 02 03
        5a a5 a1 02 a3
        5a a5 02 02 04
        5a a5 04 02 06
enable: 5a a5 11 07 ff 01 ff ff ff 15 00
disable:5a a5 11 07 ff 00 ff ff ff 14 00
```

- 帧格式：`5a a5` + cmdId(1) + len(1) + payload(len-2 B) + checksum(1)，
  checksum = `(cmdId + len + Σpayload) & 0xFF`。
- 发送后设备回 `5a a5`-前缀 ACK（response_prefix=`5a a5`，command_prefix_len=3）。
- enable 后流式输出 **`5a a5 ef` 32 字节 extended 报告**。

### V2 extended 报告布局（Vader 5，作为 Apex 5 初始假设）

| 字段 | offset | 类型 | 备注 |
|------|--------|------|------|
| 头 | 0–2 | `5a a5 ef` | match |
| left_x | 3 | i16le | |
| left_y | 5 | i16le | **取反** |
| right_x | 7 | i16le | |
| right_y | 9 | i16le | **取反** |
| 按键位图 | 11–14 | 4B | bit0=DPadUp,4=A,5=B,6=Select,7=X,8=Y,9=Start,10=LB,11=RB,14=LS,15=RS,16=C,17=Z,18=M1,19=M2,20=M3,21=M4,22=LM,23=RM,24=O,27=Home |
| lt | 15 | u8 | |
| rt | 16 | u8 | |
| gyro_x | 17 | i16le | |
| gyro_z | 19 | i16le | |
| gyro_y | 21 | i16le | 取反 |
| accel_x | 23 | i16le | |
| accel_y | 25 | i16le | |
| accel_z | 27 | i16le | |

### rumble 输出（备选，验证用）

`5a a5 12 06 {strong:u8} {weak:u8} ...`（主机→设备）。

## 3. M8 设计

### 目标

让 Apex 5 经 SLE 流式输出输入报告，逆向并验证飞智 V2 协议，产出可供后续
HID 转换使用的 32 字节报告解析。

### 架构 / 组件

- 复用 `apps/sle_probe`，新增 `sle_probe_trials.h/.c`（V2 协议试炼 + 序列器）。
- 修改 `sle_probe_client.c`：补 descriptor 发现与正确 CCC 通知开启；接入序列器。
- 不改动 default/其他 app；全部在 worktree `m8-flydigi-protocol`。

### 数据流程

```
扫描 → 连接 → 配对 → MTU → find structure(properties)
  → find structure(descriptors)  ← 新增，拿 CCC handle
  → 向 notify property 的 CCC 描述符写 0x0001  ← 修复 M7 bug
  → 启动 V2 协议序列器：
      发 init×4 + enable×1（向可写 handle 0x11/0x13）
      每条后延时 ~50ms，观察通知
  → 命中 5a a5 ef 帧 → 解析并打印轴/按键
```

### 试炼序列（首轮，replay Vader 5 V2 已知字节）

```c
static const uint8_t k_v2_init[][8] = {
    {0x5a,0xa5,0x01,0x02,0x03},   /* cmd 0x01 */
    {0x5a,0xa5,0xa1,0x02,0xa3},   /* cmd 0xa1 */
    {0x5a,0xa5,0x02,0x02,0x04},   /* cmd 0x02 */
    {0x5a,0xa5,0x04,0x02,0x06},   /* cmd 0x04 */
};
static const uint8_t k_v2_enable[11] =
    {0x5a,0xa5,0x11,0x07,0xff,0x01,0xff,0xff,0xff,0x15,0x00};
```

对可写 handle（`g_write_hdls`，M7 实测 = 0x11, 0x13）各发一遍。

### 日志增强

- 写前打印 `trial: wrote <hex> to 0xNN`
- 通知/指示回调打印完整 payload hex
- 检测 `data[0..2]==5a a5 ef` → 打印 `*** INPUT STREAM STARTED ***` + 解析
- 检测任意 `5a a5` 前缀 → 标记 `ACK`

### 错误处理 / 重连

沿用现有断链→重扫；序列中途断链重启序列。

### 验证（板上）

- `BS21_APP=sle_probe` 编译 0 warning
- 烧录 board_a，手柄进 2.4G 模式靠近
- 观察 log：是否出现 `*** INPUT STREAM STARTED ***` + 持续 `5a a5 ef` 帧
- 移动摇杆/按键，确认轴/按键位随动作变化
- 命中后记录触发命令、实际报告布局（与 Vader 5 假设的差异），更新文档

## 4. 成功判据

发送 V2 enable 后，收到 `5a a5 ef` 前缀的 32 字节帧且随手柄动作变化 → M8 成功。

## 5. 风险与回退

- **Apex 5 用 V2 变体**：enable 字节可能不同。回退：系统化微调 enable 帧
  （cmdId 0x11 不变，调 payload）；或抓官方接收器 USB 流量对照。
- **链路层差异**：SLE/SSAP 可能要求特定写 type/property。回退：尝试
  VALUE 与 DESCRIPTOR 两种 type 写可写 handle。
- **通知仍 0**：先确认 M7 bug 已修复（CCC 描述符写 0x0001 生效），再判定
  是否需要 enable。

## 6. 交付物

- `apps/sle_probe`：修正通知开启 + V2 协议序列器 + 强日志
- `docs/reference/flydigi/`：Vader5/Apex4 协议参考（已存）
- `docs/superpowers/plans/` 或 `docs/`：M8 验证记录（实际报告布局、触发命令）
- `AGENTS.md`：补充飞智 V2 协议要点
