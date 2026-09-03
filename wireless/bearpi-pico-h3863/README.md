# BearPi-Pico H3863 (WS63)

基于 WS63 (H3863) 芯片的 SLE 接收器开发平台，**新主平台**（替代 BS21）。

## 芯片规格

- SLE 1.0 + BLE 5.4 + Wi-Fi 6
- 240MHz RISC-V / 606KB SRAM

## SDK

海思 **fbb_ws63**（`~/workspace/fbb_ws63`），只读引用模式（SDK 不修改源码）。

## 目录结构

```
bearpi-pico-h3863/
├── CMakeLists.txt          # 项目根 CMake（SDK 参数 + out-of-tree 入口）
├── build.config            # Kconfig 生成（745 行，SDK 配置）
├── main/
│   └── CMakeLists.txt      # 转发器：通过 FBB_APP 选择 apps/<app>/
├── apps/
│   └── default/            # 默认 app（Hello World 验证通过）
│       ├── CMakeLists.txt
│       └── main.c
├── tools/
│   └── build.py            # 构建入口：python tools/build.py --app <app>（内置 SDK 准备）
└── sdk-compat/             # SDK 兼容层（预留）
```

## 构建

```bash
cd wireless/bearpi-pico-h3863

# 构建（build.py 内置 SDK 准备：恢复 exec 位、检查工具链）
python tools/build.py --app sle_decoy
python tools/build.py              # 等价 --app default
```

产物：`build/<app>/output/ws63/fwpkg/ws63-liteos-app/ws63-liteos-app_all.fwpkg`

每个 app 独立输出根（`build/<app>/`，已被 `.gitignore` 忽略），cmake 缓存互不
串扰。自定义输出根可设 `FBB_BUILD_ROOT_PATH`。

## 烧录与抓 log

```bash
# 烧录（共享 burn.py，自动 uart-gpio 复位）
python3 wireless/tools/burn.py board_a \
  wireless/bearpi-pico-h3863/build/sle_decoy/output/ws63/fwpkg/ws63-liteos-app/ws63-liteos-app_all.fwpkg

# 抓 log
python3 wireless/tools/capture_uart.py --board-a --duration 60 --odir /tmp --ts
```

## 多 app 结构

SDK 的 out-of-tree 构建硬编码查找工程根 `main/CMakeLists.txt` 并把 `main`
注册进 RAM_COMPONENT。因此 `main/CMakeLists.txt` 是转发器，通过 `FBB_APP`
环境变量选择 `apps/<app>/`；各 app 的 CMakeLists 必须
`set(COMPONENT_NAME "main")` 才能被链接。

添加新 app：
1. 创建 `apps/<name>/CMakeLists.txt`（`set(COMPONENT_NAME "main")`）
2. `python tools/build.py --app <name>`

## 已知问题

### Bootloader Flash Init Fail（ benign ）

上电 log 首行会显示：
```
Flash Init Fail! ret = 0x80001341
```

**根因**：SDK SFC 驱动在 bootloader 阶段返回 `ERRCODE_SFC_FLASH_NOT_SUPPORT`
（0x80001341）。板载 flash 为 GD25Q32（ID=0x1640C8），SDK 支持列表已有该芯片，
但 bootloader 的 SFC 驱动编译配置（`BUILD_APPLICATION_ROM` +
`FLASH_REGION_CFG_FLASHBOOT`，无 `CONFIG_SFC_ALREADY_INIT`）与 application 不同，
导致 `build_flash_ctrl()` 中 quad-mode 命令检查失败，回退到 unknown flash 路径。

**影响**：flash 用标准 SPI 命令正常工作，不影响 boot 和应用运行。仅失去 quad-mode
读写优化。待 OTA/升级功能需要时，再通过 sdk-compat 覆盖修复。

**状态**：已知，待后续处理。

## 开发环境搭建

详见 `wireless/bearpi-pico-h3863/docs/design.md`
