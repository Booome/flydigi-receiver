# WS63V100 NV 存储用户指南（官方文档副本）

> 来源：海思《WS63 指导资料 master》—「软件资料 / NV存储 用户指南」
> 原文：https://docs.hisilicon.com/repos/fbb_ws63/zh-CN/master/software/NV%E5%AD%98%E5%82%A8%20%E7%94%A8%E6%88%B7%E6%8C%87%E5%8D%97/%E5%89%8D%E8%A8%80.html
> 文档版本：06（2025-08-29 更新「NV项汇总」章节）
> 本文件为离线副本，便于查阅；如与线上冲突以海思官网为准。

## 概述

介绍 WS63V100 中 NV 存储模块的使用，指导快速进行二次开发。适用产品：WS63 V100。

## NV 简介

NV 模块用于在本地存储器中存储非易失性数据，每项以 key-value 方式定义：数据项含唯一
索引 key 与自定义数据类型的 value。

NV 项可通过两种方式存储：

- **编译预置**：编译阶段修改 NV 头文件与 NV 配置文件生成客制化 NV 镜像，随镜像统一烧录
  到存储介质。预置的 NV 在运行阶段可通过 API 读取和更新。
- **API 写入**：代码中调用 API 接口写入新的 NV 项。

> 须知：编译预置不支持加密 NV 项；如需加密 NV，必须用 API 接口。

## NV 编译预置

### 新增 NV 项流程

1. 在头文件中新增 kvalue 的数据类型定义（通用类型可省略）。
   - 通用类型：`uint8_t`、`uint16_t`、`uint32_t`、`bool`
   - 自定义类型路径：`middleware/chips/ws63/nv/nv_config/include/nv_common_cfg.h`
2. 在 json 文件中新增 NV 描述项。
   - NV 描述项文件路径：`middleware/chips/ws63/nv/nv_config/cfg/acore/app.json`

### NV 配置字段说明

| NV 配置选项 | 说明 |
| --- | --- |
| key_id | NV 项的 ID（十六进制，必须唯一，建议在 key_id.h 预留区间取值） |
| key_status | 标记是否将该项编进生成的 bin。`alive` = 当前固件版本使用此 key；其它/空 = 不生效 |
| structure_type | NV 项的数据结构类型 |
| attributions | 属性值（1、2、4 互斥三选一）：1=Normal（可修改）、2=Permanent（不可改）、4=Un-upgrade（不随升级修改） |
| value | NV 项的数据；结构体必须以列表书写，未赋值成员默认 0 |

配置文件示例：

```json
"common": {
    "module id": "0x0",
    "sample": {
        "key_id": "0x1",
        "key_status": "alive",
        "structure_type": "sample_type_t",
        "attributions": 1,
        "value": [1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17]
    }
}
```

### 编译生成 NV 镜像

> 须知：用 build.py 编译非 boot 目标（如 `ws63-liteos-app`、`ws63-liteos-xts`）时会默认
> 编译生成 NV 镜像并打包；默认仅包含 `ws63_all_nv.bin`，不含 `ws63_all_nv_factory.bin`。
> 若需打包 factory 版，编译命令追加：`python3 build.py -c ws63-liteos-app -def=PACKET_NV_FACTORY;`

全量编译时自动生成 NV 镜像（build_nvbin.py 执行成功）。
输出路径：`output/ws63/acore/nv_bin/ws63_all_nv.bin`，可直接烧录使用。

## NV API 指南

NV 支持存储最多 16K 数据（含管理结构占用，实际略小），备份分区与主区一致，共占 32K flash。
接口头文件：`include/middleware/utils/nv.h`。

| 接口 | 说明 |
| --- | --- |
| `uapi_nv_write(key, kvalue, kvalue_length)` | 写入 NV 项，默认属性 Normal |
| `uapi_nv_write_with_attr(key, kvalue, len, attr, func)` | 写入并配置属性/回调（WS63V100 回调传 NULL） |
| `uapi_nv_read(key, kvalue_max_length, kvalue_length, kvalue)` | 读取指定 NV 项 |
| `uapi_nv_read_with_attr(key, max_len, len, kvalue, attr)` | 读取 NV 项及其属性 |
| `uapi_nv_get_store_status(status)` | 获取 NV 存储空间使用情况 |
| `uapi_nv_set_restore_mode_all()` | 设置全量恢复出厂标记（复位后生效） |
| `uapi_nv_set_restore_mode_partitial(restore_mode)` | 设置部分 region 恢复出厂标记（复位后生效） |

- 非加密 NV 项单条有效数据最大 4060 Byte；加密项 4048 Byte。
- 恢复出厂最小单位为一个 region，不支持单独 NV 项恢复。
- region 划分：region_0 = [0x0001,0x1000)，region_1 = [0x1000,0x2000)，…… region_2 覆盖
  [0x2000,0x3000)（本表多数 BTC/BSLE 键落在此区间）。

> 注意：NV 写入会增加 flash 擦写次数，避免频繁写入；电池类建议只在关机前写。

## NV 项汇总（重点：BSLE/BTC 段）

### BSLE / BTC 配置键（0x20A0–0x20AD）

| NV_ID | NV 说明 | NV value 说明 |
| --- | --- | --- |
| 0x20A0 | BSLE 最大功率档位 | 设为 7 可用档位 0~7（-6、-2、2、6、10、14、16、20）；设为 3 可用 0~3（-6、-2、2、6） |
| 0x20A1 | BSLE GOLDEN 板开关 | 0 关闭 / 1 开启；默认关闭，仅 golden 板手动打开 |
| 0x20A4 | BSLE 第 7 档目标功率值 | 默认 20；设 20 → 每档 -6..20；设 22 → 每档 -4..22；范围 18~23 |
| 0x20A5 | BSLE 边带降功率开关 | 默认 0；1 开启边带降功率，0 关闭 |
| 0x20A6 | BSLE 边带降功率信道 | 默认 0,78,255...；表示第 0、78 信道降功率，255 不降；8 个自定义信道 |
| 0x20A7 | BSLE 边带降功率值 | 默认 8,12,255...；与 0x20A6 一一对应，第 0 信道降到 8dBm、第 78 信道降到 12dBm，255 不降 |
| 0x20A8 | SLE 调度排布间隔 | 默认 8；payload 发送+ifs+接收+ifs；范围 5~8 |
| 0x20A9 | BSLE 解调模式切换开关 | 默认 0；0 差分维特比解调 / 1 差分解调 |
| 0x20AA | BSLE 调度预排开关 | 默认 1；0 关闭调度预排 / 1 打开调度预排 |
| **0x20AB** | **BSLE channel scan 开关** | **默认 1；0 关闭 channel scan / 1 打开 channel scan** |
| 0x20AD | BSLE 上电校准开关 | 默认 0x80000000 打开；00000000 关闭（性能会恶化） |

### 其它常用键

| NV_ID | NV 说明 |
| --- | --- |
| 0x03 / 0x04 | 保留 NV 项 |
| 0x05 | MAC 地址（6 字节） |
| 0x06 | 频偏温补开关（0/1） |
| 0x07 | 频偏温补补偿值（8 个，范围 [-127,127]，对应 8 个温度区间） |
| 0x2003 | Wi-Fi 国家码（67='C'，78='N'） |
| 0x2004–0x2019 | Wi-Fi 协议特性开关（漫游/11r/TXBF/LDPC/STBC/DCM/AMSDU/AMPDU 等） |
| 0x2050–0x205C | Wi-Fi 射频插损、校准、大区功率（FCC/ETSI/JAPAN/通用）、RSSI 补偿、拟合曲线 |
| 0x205D | Wi-Fi 认证开关 |
| 0x2100–0x211F | 雷达性能参数 |
| 0x2140 | 雷达控制参数 |

## 本项目关联说明

- **SLE 广播单信道问题的官方修复**：本仓库 `tools/build.py` 构建前自动把
  `middleware/chips/ws63/nv/nv_config/cfg/acore/{app,perf}.json` 中 **`btc_channel_scan_switch`
  （0x20AB）由默认 1 改为 0**（即官方语义「关闭 channel scan」），使链路层
  `lm_chnl_scan_get_gle_chnl_map()` 不再把 host 下发的广播 `channel_map`（0x07，3 个广播信道）
  与共存 air-used 信道表做 AND，从而恢复多信道广播。
  - 反汇编依据（libbgtp.a）：`(g_macro_cfg_flags & 0x04)` 决定是否套用该 AND 掩码；
    而 `g_macro_cfg_flags` 的 bit2 在 `bt_customize_support_config()` 中依据从 NV 0x20AB 读入
    的 `g_bt_customize.byte23`（byte23==0 → `andi a4,a4,-5` 清 bit2）决定。
  - 因 0x20AB 为 `Normal`（attributions:1、可写），亦可用 `uapi_nv_write(0x20AB, &{0}, 1)`
    在运行期写入（本仓库采用编译预置方式）。
- **注意**：0x20AB 只解决「信道数」，不解决 WS63 广播报文与 BS2x 家族的**格式兼容性**问题
  （本表内无相关键，属控制器/ROM 侧编码，参见如般微/海思 release note 的「改 SDK 版本修广播
  数据格式」类修复）。
