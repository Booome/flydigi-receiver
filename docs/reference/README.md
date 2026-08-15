# 官方文档存档

本目录存放调研过程中获取的官方文档本地副本，便于离线查阅。所有文档均于 2026-08 获取。

## 目录结构

```
docs/reference/
├── ai-thinker/    # 安信可官方（Ai-BS21-32S-Kit 硬件 + Ai-BS21_SDK）
├── hisilicon/     # 海思官方（fbb_bs2x SDK）
└── ws63flash/     # 社区烧录工具（逆向 BurnTool 的协议资料）
```

## ai-thinker/ — 安信可官方

| 文件 | 说明 | 来源 |
|------|------|------|
| `Ai-BS21-32S-Kit-specification.pdf` | Kit 开发板规格书（16 页） | [规格书](https://aithinker-static.oss-cn-shenzhen.aliyuncs.com/docs/Specification/Ai-BS21-32S-Kit_V1.1.0_%20Specification_CN.pdf) |
| `Ai-BS21-32S-specification.pdf` | Ai-BS21-32S 模组规格书（21 页） | [规格书](https://aithinker-static.oss-cn-shenzhen.aliyuncs.com/docs/Specification/Ai-BS21-32S_V1.1.0_Specification_CN.pdf) |
| `Ai-BS21-32S-Kit-schematic.pdf` | Kit 开发板原理图（3 页，含 IO 引脚映射） | [原理图](https://aithinker-static.oss-cn-shenzhen.aliyuncs.com/docs/_media_old/21051160_ai-bs21-32s-kit_v1.0_%E5%8E%9F%E7%90%86%E5%9B%BE-20240218.pdf) |
| `Ai-BS21_SDK-README.md` | 安信可 SDK 说明（环境搭建、芯片选型） | [Gitee](https://gitee.com/Ai-Thinker-Open/Ai-BS21_SDK) |
| `bs21.json` | 安信可 bs21 target 配置（编译目标 `standard-bs21e-1200e`） | 同上 |
| `bs21-config.py` | 安信可 bs21 target 编译配置（含 `bs21-n1100` 芯片标识、`XO_32M_CALI` 等） | 同上 |

## hisilicon/ — 海思官方（fbb_bs2x）

| 文件 | 说明 | 来源 |
|------|------|------|
| `fbb_bs2x-README.md` | fbb_bs2x SDK 说明（芯片规格、目录、购买渠道） | [GitCode](https://gitcode.com/HiSpark/fbb_bs2x) / Gitee 镜像 |
| `bs21e.json` | fbb_bs2x bs21e target 配置（编译目标 `standard-bs21e-1100e`） | 同上 |
| `fbb_bs2x-packet.py` | fwpkg 打包入口脚本 | 同上 |
| `fbb_bs2x-packet_create.py` | fwpkg 格式定义（header 结构、CRC） | 同上 |

## ws63flash/ — 社区烧录工具（GPLv3）

| 文件 | 说明 | 来源 |
|------|------|------|
| `README.md` | ws63flash 使用说明（逆向 BurnTool 实现） | [GitHub](https://github.com/goodspeed34/ws63flash) |
| `ws63defs.h` | WS63 烧录协议帧格式（`EF BE AD DE` 帧头 + 命令 0xf0/0x5a/0xd2/0x87） | 同上 |
| `fwpkg.h` | fwpkg 文件格式解析（magic `0xefbeaddf`） | 同上 |

## 关键结论备忘

- **芯片型号对应**：安信可 "BS21"（Hi2821）= 海思 "BS21E"（Hi2821E），规格一致（160KB RAM / 1MB Flash / 12Mbps / SLE 1.0）。但安信可 SDK 芯片标识为 `bs21-n1100`，海思为 `bs21e`，版本号不同（安信可 `standard-bs21e-1200e` vs 海思 `standard-bs21e-1100e`），**存在差异需验证**。
- **fwpkg 格式**：BS2X 与 WS63 一致（magic `0xefbeaddf` + header + bin_info），海思 FBB 框架统一。
- **烧录协议**：串口烧录帧 `EF BE AD DE` + 命令集（0xf0 握手 / 0x5a 波特率 / 0xd2 下载 / 0x87 复位）。
