# BS21 协议栈补丁方案重新论证

## 1. 背景与目标

要让 `probe（board_a，client）↔ decoy（board_b，server）` 的服务发现输出与
真机手柄完全一致（`find_property` / `find_structure` 的 handle、oper、UUID
逐字段相同），后期数据流分析可用 probe↔decoy 链路稳定复现。

此前通过 `patch_gle_decoy.py` 对 SDK 闭源库 `libbth_gle.a` 打字节补丁，过程中
机制混杂（redefine + 字节 patch 并存）、有些机制实际未生效，代码库不清洁。
本文重新论证每个改动的必要性与最合理的实现方式。

## 2. 关键参考：星闪开源协议栈

`openharmony/communication_nearlink_service`（2026-07 开源，AtomGit）提供了
SSAP 协议的权威实现。逆向/对比它得到了决定性结论：

### 2.1 handle 起始值
```c
// services/stack/src/cp/bsl/sle/servm/ssap/src/ssaps_service.c
#define SSAP_INIT_HANDLE 0x0010
static uint16_t g_curHandle = SSAP_INIT_HANDLE;
```
**服务 handle 从 0x10 开始是 SSAP 协议标准**。真机（Flydigi 栈）也如此。
BS21 闭源库把游标初始化为 1，是**偏离标准的实现**。

### 2.2 operate_indication 是 32 位字段
`SSAP_FIND_OPERATION_LEN = sizeof(uint32_t)`。真机 oper=0x30d 完全合法。
BS21 的 `check_property_info` 对 `oper > 0x100` 返回 PARAM_ERR，是**多余限制**。

### 2.3 UUID 编码存在新旧两个协议版本
- V1.0 旧版：属性条目 `[hdl:2][oper:4][desc_cnt:1][types...]`，**不含 UUID**，
  解码端用「请求携带的 UUID」回填 → 显示 `len=2`
- 新版（BS21/开源栈默认）：条目内嵌 UUID（16 位紧凑或 128 位完整）→ 显示 `len=16`
- 完整版协议栈按「对端设备类型」（`CM_DEVTYPE_OLD`，配对时由密钥协商能力判定）
  自动选择格式（`SendFindPropertyRspV10` / `SendFindPropertyRsp`）

### 2.4 关键差异：BS21 是精简版
BS21 闭源 SDK 的 server 只实现单一响应格式；完整版（开源栈）两端对称且带
设备类型自适应。这是 SDK 版本裁剪的结果，不是协议设计如此。

## 3. 现有补丁逐一论证

### 3.1 handle 起始 0x10（补丁 1 + 4 + 5）

| 补丁 | 位置 | 作用 |
|------|------|------|
| c.li a5,1→16 | ssaps_add_service_core | 镜像层首服务 handle=0x10 |
| srli→c.li a5,1 | ssaps_register_server | 游标初值 0，触发上面分支 |
| c.li s2,1→16 | cs_range_allocate | ATT 层游标=0x10 |

**结论：必要。** 依据 2.1，这是把 BS21 的非标行为（handle 从 1 起）修正到协议
标准（0x10 起）。真机也是标准行为，三者自然一致。开源栈通过源码宏控制；
BS21 无任何 API/配置可改，只能字节补丁。

### 3.2 oper 校验放宽（补丁 2）

`check_property_info: li a5,256 → li a5,-1`。

**结论：必要。** 依据 2.2，oper=0x30d 是合法 32 位值，BS21 的 256 上限是多余
限制。真机必需 0x30d，必须放宽。

### 3.3 ~~redefine 机制~~（已删除）

原方案：objcopy `--redefine-sym=check_property_info=decoy_check_property_info_orig`
+ 应用提供同名替代实现。

**结论：从未生效，已删除。** 验证过程：
- `check_property_info` 是 static（local）符号，redefine 后仍是 local
- 库内调用方的重定位仍解析到库内改名后的函数体
- 应用的 global 同名实现因未被引用被 `--gc-sections` 裁剪
- ELF 实测只有库内 local 版，应用版不存在
- 尝试 `--globalize-symbol` 后，库内同时存在定义与引用同名 global，
  与应用实现重复符号冲突；objcopy 拒绝 strip 被引用的符号
- **字节补丁 2 才是 oper 放宽的唯一生效机制**，redefine 是噪音

## 4. 更合理的实现方式评估

| 方式 | 可行性 | 结论 |
|------|--------|------|
| 应用层 API 指定 handle/oper | SDK 无此类 API（ssaps_add_service 自动分配 handle，oper 无覆盖点） | 不可行 |
| 符号级替换（objcopy redefine） | local 符号陷阱 + 重复符号冲突，objcopy 限制 | 不可行 |
| 切换开源协议栈 | DLI 层是空桩，BS21 controller 接口私有无文档 | 数周~数月工程 |
| 字节补丁（现方案） | 可行，唯一现实路径 | 采用，但需做干净 |

字节补丁虽不优雅，但在"闭源库 + 无 API"的约束下是唯一可行方式。合理的做法
是把它做干净：

1. **每个补丁标注协议依据**（SSAP_INIT_HANDLE、32 位 oper），定位到开源栈对应代码
2. **删除无效机制**（redefine + 应用死代码）——已完成
3. **上下文校验**（`expected_count` + 原字节比对）防 SDK 升级静默失效——已有
4. **参数化 SDK 路径**、dry-run——待完善

## 5. 遗留问题：UUID 短格式

依据 2.3，真机的 `len=2` 是 V1.0 旧版协议行为。BS21 server 没有 V1.0 发送路径
（也没有设备类型自适应）。要让 decoy 发 V1.0 格式需复刻整个响应组装逻辑，
风险高、收益是显示形式一致。此前多次尝试（li s7、响应码、bit3）均因
"单点改动破坏 client 解析假设"而失败。

**结论：作为已知差异记录，不强行解决。** 依据：
- 功能字段（handle/oper/desc/types）已全部对齐
- dongle 已能连接 decoy 并保持会话（长格式对其不构成阻塞）
- 数据流分析（notify/read/write）不受 find 响应格式影响
- 若未来发现 dongle 功能确因 find 格式失败，再以开源 V10 布局为参照
  精确复刻（有完整源码，非盲试）

## 6. 最终清单

| 机制 | 状态 | 依据 |
|------|------|------|
| 字节补丁 1+4+5（handle 0x10） | 保留 | 开源 SSAP_INIT_HANDLE=0x0010 |
| 字节补丁 2（oper 放宽） | 保留 | oper 是 32 位字段，256 上限非标 |
| redefine + 应用实现 | 已删除 | 从未生效（local 符号陷阱） |
| UUID 短格式 | 记录为已知差异 | V1.0 协议代差，不强求 |

## 7. 后续

- 完善 patch_gle_decoy.py 健壮性（SDK 路径参数化、dry-run、版本指纹）
- 如需 UUID 短格式：以开源 `SendFindPropertyRspV10` 布局为参照做精确复刻，
  单独评估后再动