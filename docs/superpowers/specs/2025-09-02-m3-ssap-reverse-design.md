# M3 SSAP 协议逆向设计

## 目标

通过 **Decoy**（手柄模拟器）和 **Probe**（dongle 模拟器）两个工程，逐步完整捕获 dongle ↔ 手柄之间的 SSAP 通信流程。

最终产出：
- Decoy 能完全替代真实手柄与 dongle 通信
- Probe 能完全替代真实 dongle 与手柄通信
- 完整的 SSAP 协议文档（`docs/sle-ssap-protocol.md`）

## 架构

```
wireless/bearpi-pico-h3863/
├── apps/
│   ├── sle_server/          # M2 成果，保持不变
│   ├── sle_client/          # M2 成果，保持不变
│   ├── sle_decoy/           # M3 新建：SLE server 角色（对接 dongle）
│   └── sle_probe/           # M3 后续：SLE client 角色（对接手柄）
└── common/
    ├── sle_uuid.c           # UUID 编解码（base UUID + 2-byte 短码）
    └── sle_uuid.h
```

Decoy 与 Probe 结构对称：Decoy 是 server 端，Probe 是 client 端。共享层仅包含已验证可复用的 UUID 工具，不强加抽象。

## Decoy 设计

### 文件结构

```
sle_decoy/
├── main.c              # 入口
├── sle_decoy.c         # 协议引擎：回调处理 + PDU 日志
├── sle_decoy.h
├── sle_decoy_adv.c     # 广播配置
└── CMakeLists.txt
```

### PDU 日志格式

每条请求/响应一行，便于后续分析：

```
[RCV] Exchange Info Req  mtu=520 ver=1
[SND] Exchange Info Rsp  mtu=300 ver=1.1
[RCV] Find Structure Req findType=0 start=0x0001 end=0xFFFF
[SND] Find Structure Rsp count=0
[RCV] Read by UUID Req   uuid=0x1234
[SND] Read by UUID Rsp   status=0x0 len=4 data=01 02 03 04
```

### 阶段响应策略

| 阶段 | 响应内容 | 观测目标 |
|------|---------|---------|
| 1 | Exchange Info 正常回复；其余返回空 | dongle 发起什么命令、时序 |
| 2 | 逐步增加 service/property 定义 | dongle 何时进入下一阶段 |
| 3 | 完整模拟手柄响应 | dongle 进入数据通信 |

## 迭代策略

Decoy 与 Probe 同步推进，用 reset 切换（不损伤芯片，不断电不拔固件）：

```
步骤 1: Decoy 上电运行，同时拉低 Probe 的 reset 引脚（防止干扰）
        dongle 插真机位
        → 日志记录 dongle 的命令序列

步骤 2: Decoy 拉 reset，释放 Probe 的 reset
        Probe 上电运行，连真实手柄
        → Probe 模拟步骤 1 中 dongle 的命令序列
        → 从手柄获取真实响应

步骤 3: Probe 拉 reset，释放 Decoy 的 reset
        Decoy 上电，加载步骤 2 的真实响应数据
        → dongle 收到与手柄一致的响应
        → 观察 dongle 下一步行为

循环步骤 2-3，逐步推进
```

## 共享层

`sle_uuid.h/c` 提供 UUID 编解码：
- `sle_set_uuid_base(sle_uuid_t *out)` — 写入标准 base UUID
- `sle_set_uuid_u2(uint16_t u2, sle_uuid_t *out)` — 写入 2-byte 短码（little-endian 在 offset 14）

SDK 已定义 `SSAP_FIND_TYPE_*`、`SSAP_PROPERTY_TYPE_*`、`SSAP_PERMISSION_*`、
`SSAP_OPERATE_INDICATION_BIT_*` 等枚举，无需重复定义。

## 已知问题

- **Flash Init Fail (0x80001341)**：GD25Q32 在 SDK 支持列表中，但 bootloader SFC 驱动因
  构建配置差异返回错误。功能不受影响，仅在 OTA 场景需要关注。

## 后续

- M3 完成后，Decoy 和 Probe 合并为一个完整的 dongle 替代品
- 编写 `docs/sle-ssap-protocol.md` 完整协议文档
