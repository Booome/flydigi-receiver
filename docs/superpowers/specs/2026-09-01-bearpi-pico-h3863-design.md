# BearPi-Pico H3863 开发环境搭建 — 设计文档

## 一、背景与目标

### 1.1 项目背景

飞智八爪鱼5 手柄使用星闪 SLE 1.0 进行 2.4GHz 无线通信。当前项目基于 Ai-BS21-32S-Kit（BS21 芯片，Hi2821）开发 SLE 接收器，已有完整的构建/烧录/调试基础设施和 SLE/SSAP 协议实验固件。

### 1.2 新硬件

用户拿到 BearPi-Pico H3863 模块（小熊派），芯片为 WS63 (H3863)：

| 特性 | BS21 (Hi2821) | WS63 (H3863) |
|------|--------------|--------------|
| **CPU** | RISC-V 32-bit, 64 MHz | RISC-V 32-bit, 240 MHz |
| **SRAM** | 160 KB | 606 KB |
| **ROM** | — | 300 KB |
| **Flash** | 1 MB | 4 MB |
| **SLE** | SLE 1.0, 12 Mbps | SLE 1.0, 12 Mbps |
| **BLE** | BLE 5.4 | BLE 5.4 |
| **Wi-Fi** | 不支持 | Wi-Fi 6 (802.11ax) |
| **USB** | USB 2.0 | USB Type-C（烧录+调试） |
| **SDK** | Ai-BS21_SDK | fbb_ws63 |

### 1.3 目标

WS63 替代 BS21 作为新的 SLE 接收器主平台。原因：
- 性能大幅提升（240MHz vs 64MHz，606KB SRAM vs 160KB）
- 4MB Flash 可容纳更完整的协议栈和功能
- Wi-Fi 6 为未来 OTA/配置/数据上报预留能力

### 1.4 第一里程碑

Hello World + 烧录跑起来：
- 目录骨架搭建
- SDK 集成（out-of-tree overlay）
- 最小应用（LED 点亮 + 串口打印）
- 编译 → 烧录 → 串口验证

## 二、目录结构与命名

### 2.1 目录布局

```
wireless/
├── ai-bs21-32s-kit/          # 原 bs21 重命名（git mv，保留历史）
│   ├── apps/
│   ├── src/
│   ├── scripts/
│   ├── tools/
│   ├── CMakeLists.txt
│   └── ...
│
└── bearpi-pico-h3863/        # 新建（WS63 平台）
    ├── main/                 # SDK 硬编码入口，转发到 apps/<app>/
    │   └── CMakeLists.txt
    ├── apps/                 # 多 app：default / sle_probe / flydigi_decoy ...
    │   └── default/
    │       ├── CMakeLists.txt
    │       └── main.c
    ├── scripts/              # setup-sdk.sh / build.sh
    ├── tools/                # 调试/抓包工具
    ├── CMakeLists.txt        # out-of-tree 入口
    ├── build.config          # Kconfig 配置
    └── sdk-compat/           # SDK 补丁/适配（如需要）
```

> **关键约束**：WS63 SDK 的 out-of-tree 构建硬编码查找工程根 `main/CMakeLists.txt`，
> 并把 `FBB_PROJECT_COMPONENT_NAME`（默认 `main`）注册进 `RAM_COMPONENT`。因此
> `main/CMakeLists.txt` 作为转发器，通过 `FBB_APP` 环境变量选择 `apps/<app>/`；
> 各 app 的 CMakeLists 必须 `set(COMPONENT_NAME "main")` 才能被构建链接。

### 2.2 命名原则

- 使用具体开发板型号名，而非芯片型号
- `ai-bs21-32s-kit` = 安信可 Ai-BS21-32S-Kit
- `bearpi-pico-h3863` = 小熊派 BearPi-Pico H3863
- 两个平台目录并列，结构对齐，方便代码迁移对照

## 三、SDK 集成方式

### 3.1 Out-of-tree Overlay 模式

`bearpi-pico-h3863` 只读引用 WS63 SDK（`~/workspace/fbb_ws63`），不修改 SDK 源码。

SDK 提供了官方支持的 out-of-tree 构建入口：`build/cmake/project.cmake`。

### 3.2 构建方式（build.py 驱动）

**实测结论**：WS63 SDK 的 out-of-tree 构建**不能**用裸 `cmake -S . -B build`——
裸 cmake 无法注入完整参数集（`RAM_COMPONENT`/`DEFINES`/`CCFLAGS`/工具链/
`BOARD`/`ARCH_FAMILY`），导致 `RAM_COMPONENT` 为空、SDK 组件全部被跳过，
产出畸形镜像。

必须使用 SDK 官方的 `build.py` 流程（与 `fbb build` CLI / SDK sample project
相同）。`build.py` 从 ws63 目标配置推导完整 `-D` 参数集，并通过环境变量
`FBB_PROJECT_DIR` 把 cmake 源码目录翻转到本工程，实现 out-of-tree。

**构建命令**（封装在 `scripts/build.sh`）：

```bash
export FBB_PROJECT_DIR=<工程根>
export FBB_PROJECT_TARGET=ws63-liteos-app
export FBB_KCONFIG_CONFIG=<工程根>/build.config
export FBB_APP=default          # 选择 apps/<app>/
cd $FBB_SDK_DIR
python3 build.py ws63-liteos-app
```

**顶层 `CMakeLists.txt`**（仅设置 SDK 必需的全局参数，不再 `add_subdirectory(src)`）：

```cmake
cmake_minimum_required(VERSION 3.14.1)
if(NOT DEFINED FBB_SDK_DIR)
    set(FBB_SDK_DIR "$ENV{HOME}/workspace/fbb_ws63/src")
endif()
set(CHIP "ws63")
set(BIN_NAME "ws63-liteos-app")   # 必须等于 SDK 打包管线硬编码的目标名
set(CORE "acore")
set(BOARD "evb")
set(ARCH "riscv31")
set(ARCH_FAMILY "riscv")
set(BUILD_TARGET_NAME "ws63_liteos_app")
set(APPLICATION "application")
# SDK 默认 ccflags 含 -Werror 及 -DBOARD_ASIC，只能 APPEND 不能整体覆盖
list(APPEND CCFLAGS -Wno-unused-parameter ...)
list(APPEND DEFINES BOARD_ASIC)   # wifi 组件查 DEFINES
include("${FBB_SDK_DIR}/build/cmake/project.cmake")
project(${CHIP}_CFBB C ASM)
```

**`main/CMakeLists.txt`**（SDK 硬编码入口，转发到 app）：

```cmake
if(NOT DEFINED ENV{FBB_APP})
    set(FBB_APP "default")
else()
    set(FBB_APP "$ENV{FBB_APP}")
endif()
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../apps/${FBB_APP}" ...)
```

**`apps/<app>/CMakeLists.txt`**（每个 app 一个目录，`COMPONENT_NAME` 必须为 `main`）：

```cmake
set(COMPONENT_NAME "main")   # 必须匹配 RAM_COMPONENT 中的 "main"
set(SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.c)
set(MAIN_COMPONENT true)
build_component()
```

### 3.3 setup-sdk.sh 脚本

功能：
- 检查 `FBB_SDK_DIR` 环境变量或自动探测 `~/workspace/fbb_ws63`
- 验证 SDK 内置工具链存在（`tools/bin/compiler/riscv/cc_riscv32_musl_105/...`）
- 输出配置摘要

### 3.4 工具链

WS63 SDK 内置 `riscv32-linux-musl-gcc`，无需外部安装。

## 四、第一里程碑实施范围

### 4.1 范围内

1. **目录骨架**：`apps/src/scripts/tools/CMakeLists.txt`
2. **setup-sdk.sh**：SDK 路径探测与验证
3. **最小 main.c**：GPIO 点亮蓝色 LED + 串口打印 "Hello from BearPi-Pico H3863"
4. **CMake 编译**：产出可烧录镜像
5. **烧录**：用现有 `ws63flash` 工具烧录
6. **串口验证**：看到打印输出

### 4.2 范围外

- SLE/SSAP 功能（第二里程碑）
- USB HID 输出（后续阶段）
- 完整的连接管理/配对逻辑（从 ai-bs21-32s-kit 迁移时再实现）

### 4.3 烧录方式

BearPi-Pico H3863 板载 USB Type-C 支持烧录。需确认是否需要手动复位进入 bootloader，脚本化流程待实测。

## 五、后续里程碑

| 里程碑 | 内容 | 依赖 |
|--------|------|------|
| M2: SLE 基础 | server/client 广播、连接、数据收发 | M1 |
| M3: SSAP 协议 | 属性发现、读写、与 ai-bs21-32s-kit 的 probe/decoy 对齐 | M2 |
| M4: 接收器固件 | 迁移连接管理、配对、NV 记录等 | M3 |
| M5: USB HID | 作为 USB HID 设备向 PC 报告手柄输入 | M4 |

## 六、风险与待确认项

| 风险 | 说明 | 缓解 |
|------|------|------|
| 烧录方式未知 | BearPi-Pico H3863 可能需要特定操作进入 bootloader | 实测确认，参考小熊派文档 |
| SDK Kconfig 配置 | WS63 SDK 使用 Kconfig 配置外设，需了解配置方式 | 参考 SDK 内 sle_hello 示例 |
| 串口引脚 | 需确认调试串口的 UART 编号和引脚 | 查原理图或文档 |
| 工具链兼容性 | SDK 内置 musl toolchain 与项目现有工具链不同 | 隔离在 SDK 内部，不影响项目其他部分 |
