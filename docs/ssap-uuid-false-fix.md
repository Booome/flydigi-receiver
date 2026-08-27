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

## 当前待办

- 获取真机 find type=3 完整原始响应（0x17/0x18 的 value 未观测）
- 让 decoy 每个属性注册与真机一致的完整 UUID（value = 103c 等）
- 验证 encode 如何从注册 UUID 产生 value 字段（实验：注册 16B UUID 看 xx）
- dongle 实测判断其用 UUID 还是 handle 访问属性