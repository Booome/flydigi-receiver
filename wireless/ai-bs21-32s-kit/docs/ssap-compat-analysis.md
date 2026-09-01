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

## 7. 协议版本与 Flydigi 协议栈推断

### 7.1 设备类型检测机制

开源协议栈通过 `CM_SetDeviceLinkDeviceType` 检测设备类型：

```c
if ((hasVerBit) || (link->protocolVersion != CM_INVALID_VERSION)) {
    link->devType = CM_DEVTYPE_NEW;  // 支持 version 交换
    return;
}
link->devType = CM_DEVTYPE_OLD;  // 不支持 version 交换
```

- **CM_DEVTYPE_OLD**：不支持 version 交换的设备 → 使用 V10 格式 (`05 0b 00 87`)
- **CM_DEVTYPE_NEW**：支持 version 交换的设备 → 使用主路径格式 (`05 03`)

### 7.2 Flydigi 协议栈推断

**观察到的现象**：
- 真机响应格式：`05 03`（主路径，无 count 字节）
- BS21 SDK 默认：`05 0b 00 87`（V10 格式）

**推断**：
Flydigi 可能使用以下协议栈之一：

1. **旧版海思 SDK**：早期版本默认使用主路径格式（无 V10 变体）
2. **定制协议栈**：基于 SSAP 早期规范，不支持 version 交换
3. **设备类型被识别为 OLD**：即使使用新 SDK，如果设备不支持 version 交换，也会回退到 V10 格式

### 7.3 协议栈版本对比

| 协议栈 | Find 响应格式 | 设备类型检测 | 说明 |
|--------|--------------|-------------|------|
| 开源最新版 | 自动切换 | 支持 | 根据对端设备类型自动选择 V10/主路径 |
| BS21 SDK | 仅 V10 | 不支持 | 精简版，固定 V10 格式 |
| Flydigi（推断） | 仅主路径 | 不支持 | 可能使用旧版 SDK 或定制栈 |

### 7.4 SDK 对比分析

#### 7.4.1 星闪 SDK 全景

| SDK | 芯片平台 | SLE 支持 | CM_DEVTYPE | 说明 |
|-----|----------|----------|------------|------|
| **fbb_bs2x** | BS21 (Hi2821) | ✅ | ❌ | 海思官方 BS2x SDK |
| **fbb_ws53** | WS53 | ✅ | ❌ | 海思官方 WS53 SDK |
| **fbb_ws63** | WS63 | ✅ | ❌ | 海思官方 WS63 SDK |
| **hs-fbb** | 未知 | ✅ | ❌ | 海思内部 SDK |
| **开源栈** | 通用 | ✅ | ✅ | OpenHarmony 开源实现 |
| **Ai-BS21_SDK** | BS21 | ✅ | ❌ | 安信可基于 fbb_bs2x |

**关键发现**：
- **所有官方 SDK 都没有**设备类型检测（CM_DEVTYPE_OLD/NEW）
- 只有开源栈有完整的设备类型检测和自动格式切换
- 开源栈是**唯一**实现完整版 SSAP 协议的实现

#### 7.4.2 版本处理函数对比

| 功能 | BS21 SDK | 开源栈 |
|------|----------|--------|
| 读取本地版本 | `dm_gle_get_local_version` | ✅ |
| 读取对端版本 | `gle_read_remote_version_cfm` | ✅ |
| 版本解析 | `version_unpack` | ✅ |
| **设备类型判定** | ❌ | `CM_SetDeviceLinkDeviceType` |
| **设备类型获取** | ❌ | `CM_GetDeviceLinkDeviceType` |
| **格式自动切换** | ❌ | ✅ |

#### 7.4.3 关键差异：设备类型检测

开源栈的设备类型检测逻辑：

```c
void CM_SetDeviceLinkDeviceType(uint16_t connId, bool hasVerBit)
{
    // ...
    if ((hasVerBit) || (link->protocolVersion != CM_INVALID_VERSION)) {
        /* 收到对端version响应 */
        link->devType = CM_DEVTYPE_NEW;  // 使用主路径格式
        return;
    }
    link->devType = CM_DEVTYPE_OLD;  // 使用 V10 格式
}
```

**BS21 SDK 没有这个逻辑**：
- 只能读取对端版本
- 不能根据版本判定设备类型
- 行为固定（始终 V10 格式）

#### 7.4.4 Flydigi 协议栈推断

基于 SDK 对比，Flydigi 可能使用：

1. **定制版 HiSilicon SDK**：
   - 基于 fbb_bs2x，但修改了默认行为
   - 不使用 V10 格式，直接使用主路径
   - 这是最可能的情况

2. **旧版 HiSilicon SDK**：
   - 早期版本可能没有 V10 格式
   - 只有主路径格式（`05 03`）

3. **完全定制协议栈**：
   - 不使用海思 SDK
   - 自行实现 SSAP 协议

#### 7.4.5 为什么 BS21 SDK 默认 V10 格式？

BS21 SDK 的默认行为是 V10 格式（`05 0b 00 87`），而真机使用主路径（`05 03`）。
这可能是因为：
- BS21 SDK 是**精简版**，只实现了 V10 格式
- 完整版 SDK（如开源栈）有设备类型检测，可以自动切换
- Flydigi 使用完整版 SDK 或定制版，所以使用主路径

#### 7.4.6 WS63/WS65 可能性分析

**SDK 对比**：

| SDK | 设备类型检测 | 版本函数 | 说明 |
|-----|-------------|----------|------|
| fbb_bs2x | ❌ | `version_unpack`, `gle_read_remote_version_cfm` | BS21 平台 |
| fbb_ws63 | ❌ | `version_unpack`, `gle_device_link_set_exchange_version` | WS63 平台 |
| fbb_ws53 | ❌ | 类似 | WS53 平台 |
| 开源栈 | ✅ | `CM_SetDeviceLinkDeviceType` | 通用 |

**关键发现**：
1. **WS63 SDK 也没有**设备类型检测（CM_DEVTYPE_OLD/NEW）
2. WS63 有额外的版本函数：`gle_device_link_set_exchange_version`, `get_version_capability`
3. **未找到 WS65** 相关 SDK 或代码
4. 所有官方 SDK 都是精简版，行为固定

**结论**：
- **无法通过 SDK 行为区分 WS63/BS21**：两者都没有设备类型检测
- **无法确定控制器芯片**：需要固件 binary 才能确认
- **控制器行为**（主路径格式）无法通过标准 SDK 复现
- 控制器可能使用：定制版 SDK、完整版协议栈、或完全不同的实现

#### 7.4.7 反汇编分析结论

**BS21 SDK 对象文件分析**（`libbth_gle.a`）：
- 找到版本处理函数：`version_unpack`, `gle_read_remote_version_cfm`
- **未找到**设备类型检测：`CM_DEVTYPE_OLD/NEW`, `CM_SetDeviceLinkDeviceType`
- **结论**：BS21 SDK 缺少设备类型检测机制

**WS63 SDK 对象文件分析**：
- 找到版本处理函数：`version_unpack`, `gle_device_link_set_exchange_version`
- **未找到**设备类型检测：`CM_DEVTYPE_OLD/NEW`
- **结论**：WS63 SDK 也缺少设备类型检测机制

**与真机行为对比**：
- 真机使用主路径格式（`05 03`）→ 行为类似 `CM_DEVTYPE_NEW`
- BS21/WS63 SDK 使用 V10 格式（`05 0b 00 87`）→ 行为类似 `CM_DEVTYPE_OLD`
- **差异根源**：官方 SDK 都没有设备类型检测，无法切换到主路径

### 7.5 验证方向

1. **配对阶段抓包**：观察 version 交换过程，确认 Flydigi 是否发送 version 响应
2. **SDK 版本指纹**：对比不同版本 HiSilicon SDK 的默认行为
3. **开源栈行为**：用开源协议栈与 Flydigi 配对，观察格式选择
4. **逆向真机固件**：如果可能，dump 真机固件分析协议栈实现

## 8. 后续

- 完善 patch_gle_decoy.py 健壮性（SDK 路径参数化、dry-run、版本指纹）
- 如需 UUID 短格式：以开源 `SendFindPropertyRspV10` 布局为参照做精确复刻，
  单独评估后再动
- **验证 Flydigi 协议栈版本**：通过配对阶段抓包确认设备类型检测行为