# BearPi-Pico H3863 开发环境搭建 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 BearPi-Pico H3863 (WS63) 上跑通 Hello World：编译 → 烧录 → 串口打印。

**Architecture:** Out-of-tree CMake overlay 模式。`wireless/bearpi-pico-h3863` 自包含项目文件，只读引用 `fbb_ws63` SDK。SDK 的 `project.cmake` 提供官方 out-of-tree 构建入口。

**Tech Stack:** CMake 3.14+, riscv32-linux-musl-gcc (SDK 内置), WS63 LiteOS, ws63flash 烧录工具

## Global Constraints

- SDK 只读引用，不修改 `fbb_ws63` 源码
- 目录结构对齐 `ai-bs21-32s-kit`（apps/src/scripts/tools）
- 使用 git worktree 隔离开发（`git worktree add`）
- 所有代码/注释英文，文档中文
- C 代码修改后必须 `clang-format -i` 格式化
- 不主动 commit，等用户确认

---

## Task 1: 创建 git worktree

**Files:**
- Create: git worktree at `../flydigi-receiver-worktrees/bearpi-pico-h3863`

**Interfaces:**
- Consumes: 无
- Produces: 隔离的工作目录

- [ ] **Step 1: 创建 worktree**

```bash
cd /home/bodong/workspace/flydigi-receiver
git worktree add ../flydigi-receiver-worktrees/bearpi-pico-h3863 -b bearpi-pico-h3863
```

Expected: 新分支 `bearpi-pico-h3863` 已创建，工作目录在 `../flydigi-receiver-worktrees/bearpi-pico-h3863`

---

## Task 2: 搭建目录骨架

**Files:**
- Create: `wireless/bearpi-pico-h3863/CMakeLists.txt`
- Create: `wireless/bearpi-pico-h3863/main/CMakeLists.txt`
- Create: `wireless/bearpi-pico-h3863/main/main.c`
- Create: `wireless/bearpi-pico-h3863/scripts/setup-sdk.sh`
- Create: `wireless/bearpi-pico-h3863/.gitignore`
- Create: `wireless/bearpi-pico-h3863/sdk-compat/.gitkeep`

**Interfaces:**
- Consumes: 无
- Produces: 可构建的项目骨架

> **注意**：SDK 的 out-of-tree 构建系统硬编码查找 `${CMAKE_SOURCE_DIR}/main/CMakeLists.txt`（见 `build_core.cmake` 第 167 行），因此应用入口目录必须命名为 `main/`。

- [ ] **Step 2: 创建目录结构**

```bash
cd /home/bodong/workspace/flydigi-receiver-worktrees/bearpi-pico-h3863
mkdir -p wireless/bearpi-pico-h3863/{main,scripts,tools,sdk-compat}
```

Expected: 目录创建成功

- [ ] **Step 3: 编写顶层 CMakeLists.txt**

写入 `wireless/bearpi-pico-h3863/CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.14.1)

# SDK 路径：由 setup-sdk.sh 写入环境变量或 .env
if(NOT DEFINED FBB_SDK_DIR)
    if(DEFINED ENV{FBB_SDK_DIR})
        set(FBB_SDK_DIR "$ENV{FBB_SDK_DIR}")
    else()
        set(FBB_SDK_DIR "$ENV{HOME}/workspace/fbb_ws63/src")
    endif()
endif()

# 必须传入的 SDK 参数
set(CHIP "ws63")
set(BIN_NAME "flydigi_wireless")
set(CORE "acore")
set(BUILD_TARGET_NAME "ws63_liteos_app")
set(CCFLAGS "-Wno-unused-parameter -Wno-unused-variable -Wno-unused-const-variable")

# 引入 SDK 的 out-of-tree 入口
include("${FBB_SDK_DIR}/build/cmake/project.cmake")

# 声明项目
project(${CHIP}_CFBB C ASM)
```

- [ ] **Step 4: 编写 main/CMakeLists.txt**

写入 `wireless/bearpi-pico-h3863/main/CMakeLists.txt`：

```cmake
set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    PARENT_SCOPE
)
```

- [ ] **Step 5: 编写最小 main.c**

写入 `wireless/bearpi-pico-h3863/main/main.c`（参考 SDK 的 helloworld 示例，仅用 `osal_printk` 输出，不依赖 PINCTRL）：

```c
#include "soc_osal.h"
#include "app_init.h"

#define TASK_STACK_SIZE 0x1000
#define TASK_PRIO       26

static int hello_task(const char *arg)
{
    for (;;) {
        osal_printk("Hello from BearPi-Pico H3863!\r\n");
        osal_msleep(1000);
    }

    return 0;
}

static void hello_entry(void)
{
    osal_task *task = NULL;
    osal_kthread_lock();
    task = osal_kthread_create((osal_kthread_handler)hello_task, 0, "HelloTask", TASK_STACK_SIZE);
    if (task != NULL) {
        osal_kthread_set_priority(task, TASK_PRIO);
        osal_kfree(task);
    }
    osal_kthread_unlock();
}

app_run(hello_entry);
```

- [ ] **Step 6: 编写 setup-sdk.sh**

写入 `wireless/bearpi-pico-h3863/scripts/setup-sdk.sh`：

```bash
#!/usr/bin/env bash
set -euo pipefail

FBB_SDK_DIR="${FBB_SDK_DIR:-$HOME/workspace/fbb_ws63/src}"

if [ ! -d "$FBB_SDK_DIR" ]; then
    echo "error: WS63 SDK not found at $FBB_SDK_DIR (set FBB_SDK_DIR)" >&2
    exit 1
fi

COMPILER_ROOT="$FBB_SDK_DIR/../tools/bin/compiler/riscv/cc_riscv32_musl_105/cc_riscv32_musl"
if [ ! -d "$COMPILER_ROOT" ]; then
    echo "error: toolchain not found at $COMPILER_ROOT" >&2
    exit 1
fi

# 恢复 git clone 丢失的 exec 位
find "$FBB_SDK_DIR/../tools/bin" -type f -exec chmod +x {} \; 2>/dev/null || true

echo "WS63 SDK prepared at $FBB_SDK_DIR"
echo "Toolchain: $COMPILER_ROOT"
```

```bash
chmod +x wireless/bearpi-pico-h3863/scripts/setup-sdk.sh
```

- [ ] **Step 7: 编写 .gitignore**

写入 `wireless/bearpi-pico-h3863/.gitignore`：

```
build/
.env
*.pyc
__pycache__/
```

---

## Task 3: 配置 Kconfig

**Files:**
- Create: `wireless/bearpi-pico-h3863/build.config`

**Interfaces:**
- Consumes: SDK Kconfig 模板
- Produces: 构建配置

- [ ] **Step 1: 复制 SDK Kconfig 模板**

```bash
cd /home/bodong/workspace/flydigi-receiver-worktrees/bearpi-pico-h3863
cp "$HOME/workspace/fbb_ws63/src/build/config/target_config/ws63/menuconfig/acore/ws63_liteos_app.config" \
   wireless/bearpi-pico-h3863/build.config
```

Expected: 文件复制成功

- [ ] **Step 2: 验证 Kconfig 基础配置**

确认 `build.config` 包含以下关键配置（默认已有）：

```
CONFIG_SAMPLE_ENABLE=y
CONFIG_DRIVER_SUPPORT_GPIO=y
CONFIG_DEBUG_UART=0
CONFIG_DEBUG_UART_BAUDRATE=115200
```

无需额外修改。`osal_printk` 通过 DEBUG_UART 输出，默认 UART0 115200。

---

## Task 4: 首次构建

**Files:**
- Modify: 无（构建产物在 build/）

**Interfaces:**
- Consumes: Task 2 + Task 3
- Produces: 可烧录的 .fwpkg 镜像

- [ ] **Step 1: 运行 setup-sdk.sh**

```bash
cd /home/bodong/workspace/flydigi-receiver-worktrees/bearpi-pico-h3863/wireless/bearpi-pico-h3863
bash scripts/setup-sdk.sh
```

Expected: 输出 "WS63 SDK prepared at ..."

- [ ] **Step 2: 配置 CMake**

```bash
cd /home/bodong/workspace/flydigi-receiver-worktrees/bearpi-pico-h3863/wireless/bearpi-pico-h3863
export FBB_SDK_DIR="$HOME/workspace/fbb_ws63/src"
export FBB_KCONFIG_CONFIG="$(pwd)/build.config"
export FBB_PROJECT_TARGET="flydigi_wireless"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

Expected: CMake 配置成功，无 FATAL_ERROR

- [ ] **Step 3: 编译**

```bash
cmake --build build -j$(nproc)
```

Expected: 编译成功，产出 `build/flydigi_wireless.bin` 或 `.fwpkg`

---

## Task 5: 烧录与验证

**Files:**
- Modify: 无

**Interfaces:**
- Consumes: Task 4 构建产物
- Produces: 板子运行 + 串口输出

- [ ] **Step 1: 连接板子并确认烧录方式**

```bash
# 检查 USB 设备
lsusb | grep -i "bearpi\|ws63\|hisilicon"
# 检查串口
ls /dev/serial/by-path/ 2>/dev/null || ls /dev/ttyUSB* 2>/dev/null
```

Expected: 识别到板子的 USB 设备或串口

- [ ] **Step 2: 烧录**

```bash
cd /home/bodong/workspace/flydigi-receiver-worktrees/bearpi-pico-h3863/wireless/bearpi-pico-h3863
# 找到 .fwpkg 文件
FWPKG=$(find build -name "*.fwpkg" | head -1)
echo "Flashing: $FWPKG"
ws63flash --flash /dev/ttyUSB0 "$FWPKG" -b460800
```

Expected: 烧录成功，进度 100%

- [ ] **Step 3: 串口验证**

```bash
# 监听串口输出
picocom -b 115200 /dev/ttyUSB0
# 或
miniterm /dev/ttyUSB0 115200
```

Expected: 看到 "Hello from BearPi-Pico H3863!" 输出

---

## Task 6: 更新项目文档

**Files:**
- Modify: `AGENTS.md`
- Modify: `wireless/bearpi-pico-h3863/docs/design.md`

**Interfaces:**
- Consumes: Task 1-5 成果
- Produces: 文档同步

- [ ] **Step 1: 更新 AGENTS.md**

在 `AGENTS.md` 中添加 BearPi-Pico H3863 平台信息（参照 BS21 部分）：

```markdown
### BearPi-Pico H3863 开发板（WS63）

BearPi-Pico H3863，基于 WS63 (H3863) 芯片：
- SLE 1.0 + BLE 5.4 + Wi-Fi 6
- SDK: 海思 **fbb_ws63**（`~/workspace/fbb_ws63`），只读引用模式
- target: `ws63_liteos_app`（LiteOS, acore, 应用处理器）
- 编译：`cmake -S wireless/bearpi-pico-h3863 -B wireless/bearpi-pico-h3863/build && cmake --build wireless/bearpi-pico-h3863/build -j`
- 烧录：`ws63flash --flash <串口> <fwpkg> -b460800`
```

- [ ] **Step 2: 更新设计文档**

在设计文档末尾添加实施结果记录。

---

## 后续步骤

- M2: SLE 基础（server/client 广播、连接、数据收发）
- M3: SSAP 协议（属性发现、读写）
- M4: 接收器固件迁移
- M5: USB HID 输出
