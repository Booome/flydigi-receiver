# 掩耳盗铃式修复复盘：uuid len=2 的假象

> 本文记录一次典型的"显示对齐"式误修复，以及由此提炼的防错规则。
> 根因：把 client 侧解析的**显示值**当作数据真相，未验证 server 实际发送的**线上字节**。

## 现象与误判

- 真机 find type=3：probe 显示 `uuid len=2 37BE`
- decoy find type=3：probe 显示 `uuid len=16 37BE0000...`
- **误判**：差异是"uuid 长度"（server 发 2B vs 16B），修复方向 = 让 decoy 发 2B uuid

## 真相（uuid_setu2 机制）

probe 的 `uuid_setu2(out, value)` 实现（反汇编确认）：

```asm
uuid_setu2(out, value):
  memcpy(out+1, 16, BASE_UUID, 16)  ; BASE = 37BEA880-FC70-11EA-B720-000000000000
  out[15] = value & 0xFF
  out[16] = value >> 8
  out[0]  = 2                        ; len 固定 = 2
```

即：**probe 把 find 响应条目中的 value 字段塞进 client 本地 base UUID 的末尾 16 位**，并把 `len` 设为 2。

- probe 打印 `len=2` = **client 假象**（固定值），不反映 server 发送内容
- probe 打印前 2 字节 `37 BE` = **base 前缀**（client 常量），与 server 无关
- server 真正控制的 = 条目 value 字段 → 组装后的完整 UUID 末尾 16 位

| | 条目 value | probe 显示 | 完整 UUID（base+value） |
|---|---|---|---|
| 真机 | `103c`,`103b`,`1039`,`103a`,`103f` | len=2 37BE | ...00000000103C 等（每属性不同） |
| decoy（误修后） | `0000` | len=2 37BE | ...000000000000（全 0） |

**probe 显示一致 = 掩耳盗铃**：decoy 的完整 UUID 与真机完全不同，但显示被 base 前缀掩盖。

## 错误链条

1. 未抓**真机原始响应字节**做权威对照，仅以 probe 的解析显示作为基准
2. 未逆向 client 的 `uuid_setu2` 机制，未意识到 `len=2` 是 client 固定值
3. 为了"显示 len=2"做了一串字节 patch（uuid 16→2、uuid2 源偏移修正等），
   实际只是把 probe 推进了 `uuid_setu2` 假象路径，**掩盖而非修复**
4. 回归只看 probe 显示，不验证线上字节 → 错误被固化

## 防错规则（务必遵守）

1. **显示值 ≠ 数据真相**：probe 的解析显示可能由 client 侧机制产生（base 组装、
   回填、固定长度等）。任何"显示对齐"的修改，必须同时抓 server 发送的
   **原始响应字节**（RX/TX dump）验证真实数据。

2. **先抓真机原始字节，再谈对齐**：对齐 decoy 与真机行为，第一步永远是
   分别抓真机和 decoy 的原始响应字节逐字节对比，**不得只对比 probe 显示**。

3. **patch 前必须理解 encode+decode 双侧机制**：确认 patch 改变的是
   server 发送的"真实数据"，而非 client 的"显示路径"。若 patch 只让显示
   变对而线上字节/完整数据仍不对，即为掩耳盗铃，禁止。

4. **发现 client 侧可能造数据时立即深挖**：如观察到"显示值不随 server 输入
   变化"（本例 len=2 固定、37BE 固定），说明有 client 侧机制，必须逆向前
   弄清，不得绕过。

5. **回归必须覆盖线上字节**：`regress_find.py` 验证 probe 显示通过 ≠ 线上
   字节一致。需要时补充原始字节对比断言。

## 根因与正确修复（已验证，2026-08-27）

机制（反汇编 `ssaps_find_items_by_uuid` + 实验确认）：

- SSAP 条目节点结构：`handle@0(2B)` + `uuid@4(17B: len+16)`。
- **encode 读取 uuid 字段的最后 2 字节（u14,u15）作为 find 响应条目的 `xx` 值**
  （GATT 约定：16-bit UUID value 嵌在 128-bit UUID 末尾 2 字节）。
- 注册 **2-byte uuid** `{len=2, b0, b1, 0…0}` 时，u14,u15 = 0 → `xx=0000`
  （之前实验"注册每属性 uuid 仍 0000"正是此因）。
- 真机注册 **full 16-byte uuid**，value 放在末尾 2 字节 → `xx≠0`。

**正确修复（改真实数据，非显示）**：

1. `decoy_add_property` 改为注册 **full 16-byte uuid**（`decoy_add_uuid16`），
   value 写入 `uuid[14],uuid[15]`（LE：uuid[14]=low, uuid[15]=high）。
   7 个属性分别注册 `103c/103b/1039/103a/103f/1040/102e`。
2. `patch_gle_decoy.py` **移除**之前那串掩耳盗铃 patch：
   - `uuid 16 -> 2`（强制 2B 条目，与"显示 len=2"强相关）
   - `node+4 -> node+5`（把 xx 硬读成固定偏移的 37be，与真实注册值无关）
   - 对应的 service 变体
   - `C9 find-rsp PDU+2`（V10 计数位置 hack）
   仅保留机制正确的 4 个 patch（handle 基址 0x10、oper 上限、register_server
   cursor、cs_range 基址）。
3. 重新加回 **`uuid 16 -> 2` 的"宽度切换"**（条目发 2 字节而非 16 字节，
   以匹配真机 9 字节条目布局），但 **不改读偏移**——2-byte 分支本来就从
   u14,u15 读 value，配合 full-16 注册即得到正确 xx。

验证（抓 decoy 原始 RX 字节，对比 `real-controller-find-type3.hex`）：

```
DEC : 05 0b 00 87 11 00 10 3c 0d 03 00 00 01 02 12 00 10 3b 05 00 00 00 00 ...
REAL: 05 03       11 00 10 3c 0d 03 00 00 01 02 12 00 10 3b 05 00 00 00 00 ...
```

从首个 `11 00` 起，**每个条目（handle + xx + oper + desc）逐字节完全一致**，
`xx` = `103c/103b/1039/103a/103f/1040/102e` 与真机一致。基线见
`docs/reference/decoy-find-type3-after-xxfix.hex`。

**剩余差异（独立于本次 uuid 修复，属格式问题）**：

- 头部：`05 0b 00 87`（V10 格式：0b=03|0x08 标志，87=带标志的 count）
  vs 真机 `05 03`（主路径，无 count 字节）。
- 条目数据已完全一致；仅响应**外层格式**（V10 计数式 vs 主路径）不同。
- 该格式由 ATT/SSAP 响应编码层决定（BS21 SDK 默认 V10，真机用主路径），
  需另做一层逆向定位后才能 patch。见下"待办"。

## 待办

- [x] 获取真机 find type=3 完整原始响应（已抓 `real-controller-find-type3.hex`）
- [x] 让 decoy 注册 full-16 uuid，value=末尾 2 字节（103c 等）
- [x] 验证 encode 读 u14/u15 作为 xx（实验 + 反汇编确认）
- [x] decoy 条目字节与真机逐字节一致（基线 `decoy-find-type3-after-xxfix.hex`）
- [ ] 响应外层格式对齐：V10(`05 0b 00 87`) → 主路径(`05 03`)，需定位 ATT/SSAP
      响应编码层的格式/计数选择并 patch（独立于 uuid 修复）
- [ ] 清理 probe 的 TEMP DEBUG RX dump hook（`probe_dump_discovery_cfm` 经
      objcopy 重定义 `ssapc_discovery_services_cfm`，仅用于本次抓包验证）
- [ ] dongle 实测：插官方 dongle 端到端确认