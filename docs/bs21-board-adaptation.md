# BS21 板级适配依据

本文档说明 fbb_bs2x 固件适配安信可 Ai-BS21-32S-Kit 需要改动的地方，每一处都给出确切依据、出处文件（已入库 `docs/reference/`）和确定性等级，便于人工核实。

## 依据文件（本地入库位置）

| 文件 | 来源 |
|------|------|
| `docs/reference/ai-thinker/bs21-config.py` | 安信可 Ai-BS21_SDK 的 bs21 target 配置 |
| `docs/reference/ai-thinker/bs21.json` | 安信可 bs21 target 的 IDE 配置 |
| `docs/reference/hisilicon/fbb_bs2x-bs21e-target_config.py` | 海思 fbb_bs2x 的 bs21e target 配置 |
| `docs/reference/hisilicon/bs21e.json` | 海思 fbb_bs2x 的 bs21e IDE 配置 |
| `docs/reference/hisilicon/fbb_bs2x-clock_calibration.h` / `.c` | 海思 fbb_bs2x 晶振校准驱动 |
| `docs/reference/ai-thinker/Ai-BS21-32S-Kit-schematic.pdf` | 安信可 Kit 原理图（UART 引脚） |

---

## 适配点 1：芯片同源（结论：fbb_bs2x 支持你的 BS21 芯片）

**结论**：安信可 "BS21" 与海思 "BS21E" 是同一颗芯片（Hi2821E），硬件版本 N1100，无需改芯片，只需改板级配置。

**依据**：
- `fbb_bs2x-bs21e-target_config.py` 第 72 行：
  ```python
  'fixed_rom_path': '<root>/drivers/chips/bs2x/rom/rom_bin/rom_n1100/application_rom.bin',
  ```
  海思 bs21e target 用的 ROM 目录是 `rom_n1100`。
- `ai-thinker/bs21-config.py`（target `standard-bs21-n1100`）：
  ```python
  'pkg_chip': 'bs21-n1100',
  ```
  安信可 bs21 target 的芯片标识是 `bs21-n1100`。
- 两者都指向 **n1100** 这个 ROM/芯片版本。

**出处**：上述两文件本地入库；原始链接 [fbb_bs2x (GitCode)](https://gitcode.com/HiSpark/fbb_bs2x)、[Ai-BS21_SDK (Gitee)](https://gitee.com/Ai-Thinker-Open/Ai-BS21_SDK)。

**确定性**：高。

---

## 适配点 2：晶振校准 XO_32M_CALI（结论：需在 fbb_bs2x target 里补上）

**结论**：安信可板子启用了 32M 晶振 efuse 校准（`XO_32M_CALI`），fbb_bs2x 的默认 bs21e target **没有**启用。这是固件能否正常跑时钟的关键差异。

**依据**：
- `ai-thinker/bs21-config.py` 第 16 行（`standard-bs21-n1100` target 的 `defines`）：
  ```python
  'defines': [
      'SUPPORT_CFBB_UPG', 'BGLE_TASK_EXIST', 'SUPPORT_MULTI_LIBS', 'SW_UART_DEBUG',
      'AT_COMMAND', 'XO_32M_CALI', 'SUPPORT_SFC_IRQ_LOCK'
  ],
  ```
  安信可 target 明确定义了 `XO_32M_CALI`。
- `fbb_bs2x-bs21e-target_config.py` 第 23-31 行（`defines` 完整列表）：
  ```python
  'defines': ['-:CHIP_BS21E=1', 'LIBCPU_UTILS', ..., 'BS21E_PRODUCT_EVB', ...],
  ```
  列表里**没有** `XO_32M_CALI`。
- `fbb_bs2x-clock_calibration.h` 第 36 行起：
  ```c
  #ifdef XO_32M_CALI
  void calibration_xo_core_ctrim_init(void);
  ...
  #endif
  ```
  以及注释 `Get the XO(32M) ctrim value from efuse`，说明 `XO_32M_CALI` 控制 32M 晶振（XO）的 ctrim 校准，校准值从 efuse 读取。

**含义**：安信可模组出厂时在 efuse 里写入了晶振校准值（ctrim），且 SDK 启用了 `XO_32M_CALI` 读取它。fbb_bs2x 默认 EVB target 未启用，若直接烧录，32M 晶振可能不校准，导致主时钟偏差。

**出处**：本地入库的 `bs21-config.py`、`fbb_bs2x-bs21e-target_config.py`、`fbb_bs2x-clock_calibration.h/.c`。

**确定性**：高。

---

## 适配点 3：board 配置（结论：fbb_bs2x 默认是海思 EVB 参考板）

**结论**：fbb_bs2x 的 bs21e target 默认 `board = 'evb'`，产品宏为 `BS21E_PRODUCT_EVB`，非安信可板子。IO 复用、外设引脚需按安信可板子调整。

**依据**：
- `fbb_bs2x-bs21e-target_config.py` 第 16 行：`'board': 'evb',`
- 同文件第 25 行（`defines`）：`'BS21E_PRODUCT_EVB'`

**出处**：本地入库的 `fbb_bs2x-bs21e-target_config.py`。

**确定性**：高（board 名是 evb 是确凿的；但"IO 复用具体差在哪"还需对比两侧 pinctrl/board_config 源码，见适配点 5）。

---

## 适配点 4：SDK 版本差异（结论：版本号不同，非二进制不兼容）

**结论**：安信可 SDK 编译目标为 `standard-bs21e-1200e`，海思 fbb_bs2x 为 `standard-bs21e-1100e`。版本号不同（1200 vs 1100），但芯片同源，主要影响 SDK 功能/修复差异，非芯片不兼容。

**依据**：
- `ai-thinker/bs21.json`：`"custom_build_command": "standard-bs21e-1200e"`
- `hisilicon/bs21e.json`：`"custom_build_command": "standard-bs21e-1100e"`

**出处**：本地入库的 `bs21.json`、`bs21e.json`。

**确定性**：高。

---

## 适配点 5：UART 调试引脚（结论：安信可侧已确认，fbb_bs2x 侧待查）

**结论**：安信可 Kit 的调试串口 CH340 接到模组的 **UART0**（GPIO19_TX0 / GPIO20_RX0）。fbb_bs2x 默认 EVB 的 UART0 引脚是否一致，需查其 board_config/pinctrl 源码。

**依据（安信可侧，已确认）**：
- `Ai-BS21-32S-Kit-schematic.pdf`（原理图）中 CH340C（U1）的 TXD 接到模组的 `RX0`（GPIO20_RX0），模组引脚标注含 `GPIO19_TX0`、`GPIO20_RX0`。

**待查证（海思侧）**：
- fbb_bs2x 的 `board_config` / `pinctrl` 组件中 UART0 的默认引脚定义。查询路径：`src/drivers/chips/bs2x/porting/`（board_config 相关）或 `src/application/` 的 pinctrl 配置。

**确定性**：中（安信可侧确凿，海思侧待查）。

---

## 汇总

| # | 适配点 | 结论 | 确定性 |
|---|--------|------|--------|
| 1 | 芯片 | BS21 = BS21E，同源 N1100，无需改 | 高 |
| 2 | 晶振 | 需补 `XO_32M_CALI`（efuse 晶振校准） | 高 |
| 3 | board | fbb_bs2x 默认 evb，需改为安信可板级配置 | 高 |
| 4 | SDK 版本 | 1100e vs 1200e，功能差异非芯片差异 | 高 |
| 5 | UART 引脚 | 安信可用 UART0(GPIO19/20)，海思侧待查 | 中 |
| 6 | SDK 使能固件 | 完整 fwpkg，非硬性前置；校准值在 efuse 不受烧录影响 | 高（见下） |

## 附录：安信可"SDK 使能固件"调研

**文件**：`docs/reference/ai-thinker/init_sdk_fw.fwpkg`（已入库，magic `0xefbeaddf`）

**解析结果**（6 个镜像）：

| 镜像 | 大小 | 烧录地址 | 作用 |
|------|------|---------|------|
| `loaderboot_sign.bin` | 25120 B | 0x0 | 一级加载器 |
| `partition.bin` | 1024 B | 0x90100000 | flash 分区表 |
| `flashboot_sign_a.bin` | 37952 B | 0x90101000 | 二级 bootloader A |
| `flashboot_sign_b.bin` | 37952 B | 0x9010b000 | 二级 bootloader B（备份）|
| `application_sign.bin` | 413792 B | 0x90115000 | 应用 |
| `bs21_all_nv.bin` | 4096 B | 0x9017e000 | NV 区（校准值/MAC 等）|

**结论**：
1. "SDK 使能固件"就是一个**完整的 fwpkg**（boot 链 + 应用 + NV），作用是让模组从出厂状态进入"可跑 SDK 应用"状态，同时作为安信可的**出厂验证固件**（证明板子硬件正常）。
2. fbb_bs2x 编译的 `all_in_one.fwpkg` 结构相同（也含 loaderboot/flashboot/partition/app/nv），**可完整替代它，不是硬性前置**——直接烧 `all_in_one.fwpkg` 即可，无需先烧 init_sdk_fw。
3. init_sdk_fw 的实用价值仅是**作为"已知良好"基线验证烧录工具链**：先烧它确认"工具 + 硬件"正常，再烧自制固件时若失败即可锁定问题在固件适配。实测已完成此验证（2026-08-15，ws63flash + 460800 烧录成功，板子启动输出 test1）。
4. **晶振校准值（ctrim）有两个标准存储位置**（海思定义，非安信可私有）：
   - **efuse（OTP 一次性熔丝）**：产线校准写入。`calibration_save_xo_core_ctrim()` 用 `uapi_efuse_write_buffer(XO_CORE_CTRIM, ...)` 写，`calibration_read_xo_core_ctrim()` 用 `uapi_efuse_read_buffer(...)` 读。烧录 fwpkg 只写外部 flash，**不碰 efuse，产线校准值不会丢失**。
   - **flash NV**：运行时校准缓存。`calibration_xo_core_ctrim_save_flash()` 用 key `BTH_BLE_NV_RESERVED_ID` 存 flag+value，可被烧录覆盖但固件可重校准。
   - **初始化读取优先级**（`calibration_xo_core_ctrim_init`）：flash NV → efuse → 默认值 0。
5. flash NV 区（`bs21_all_nv.bin`）是通用出厂模板（所有板子同一份），即使被覆盖，从 `init_sdk_fw.fwpkg` 提取烧回即可恢复，无独特数据。
6. 真正要做的适配：fbb_bs2x 默认 EVB target 未启用 `XO_32M_CALI`，需启用该宏让固件读 efuse 校准值（否则固件用默认值，晶振不校准）。

> 下一步（环境就绪后）：在 fbb_bs2x 的 bs21e target 上补 `XO_32M_CALI`，对比并改 UART/pinctrl 板级配置。每处改动均以上述依据文件为准。
