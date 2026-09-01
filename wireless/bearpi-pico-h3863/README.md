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
├── scripts/
│   ├── build.sh            # 构建入口（调用 SDK build.py）
│   └── setup-sdk.sh        # SDK 准备（恢复 exec 位、检查工具链）
└── sdk-compat/             # SDK 兼容层（预留）
```

## 构建

```bash
# 一次性前置（恢复 git clone 丢失的 exec 位）
bash wireless/bearpi-pico-h3863/scripts/setup-sdk.sh

# 构建（FBB_APP 选择 app，默认 default）
FBB_APP=default bash wireless/bearpi-pico-h3863/scripts/build.sh
```

产物：`$FBB_SDK_DIR/output/ws63/fwpkg/ws63-liteos-app/ws63-liteos-app_all.fwpkg`

## 烧录与抓 log

```bash
# 烧录（共享 burn.py，自动 uart-gpio 复位）
python3 wireless/tools/burn.py board_a $FBB_SDK_DIR/output/ws63/fwpkg/ws63-liteos-app/ws63-liteos-app_all.fwpkg

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
2. `FBB_APP=<name> bash scripts/build.sh`

## 开发环境搭建

详见 `wireless/bearpi-pico-h3863/docs/design.md`
