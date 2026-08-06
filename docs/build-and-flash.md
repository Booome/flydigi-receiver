# NCS 编译与烧录指南

本文档记录 nRF Connect SDK (NCS) 的环境配置、编译和烧录方法。
适用于本项目所有固件（蓝牙接收器、2.4G 接收器等）。

## 一、环境概述

| 组件 | 版本 | 安装位置 |
|------|------|---------|
| NCS | v3.4.0 | `~/ncs/v3.4.0/` |
| 工具链 | fbf7391cab | `~/ncs/toolchains/fbf7391cab/` |
| Zephyr SDK | 14.3.0 (arm-zephyr-eabi-gcc) | 工具链内 `opt/zephyr-sdk/` |
| nrfutil | 8.2.0 | 工具链内 `nrfutil/bin/` |
| west | 1.5.0 | 工具链内 `usr/local/bin/` |

### 关键设计决策

**不全局设置 LD_LIBRARY_PATH 等环境变量**。NCS 工具链的 `environment.json`
描述了大量环境变量（LD_LIBRARY_PATH、PYTHONPATH、ZEPHYR_TOOLCHAIN_VARIANT
等），全局设置会污染系统其他程序（如 pyenv）。

**使用 `nrfutil sdk-manager toolchain launch` 隔离环境**。官方推荐方式，
在命令执行期间注入所有环境变量，执行完毕自动清理。

## 二、一次性安装

### 2.1 安装 nrfutil

从 [nRF Util Downloads](https://www.nordicsemi.com/Products/Development-tools/nrf-util)
下载 nrfutil 可执行文件，放到系统 PATH 中：

```bash
# 方式 1：下载独立二进制（推荐，不依赖 toolchain 版本）
chmod +x nrfutil
mv nrfutil ~/.local/bin/

# 方式 2：从 toolchain 符号链接（toolchain 版本变化时需更新）
ln -sf ~/ncs/toolchains/fbf7391cab/nrfutil/bin/nrfutil ~/.local/bin/nrfutil
```

### 2.2 安装 nrfutil 子命令

```bash
nrfutil install device        # 设备管理/烧录
nrfutil install sdk-manager   # SDK/工具链管理
nrfutil install nrf5sdk-tools # DFU 包生成（Dongle 烧录用）
```

### 2.3 安装 NCS 和工具链

```bash
nrfutil sdk-manager search              # 查看可用版本
nrfutil sdk-manager install v3.4.0      # 安装 NCS v3.4.0 + 工具链
```

安装完成后目录结构：

```
~/ncs/
  v3.4.0/              # NCS 源码（west workspace）
    zephyr/
    nrf/
    nrfxlib/
    modules/
    bootloader/
  toolchains/
    fbf7391cab/         # 工具链（版本哈希命名）
      opt/zephyr-sdk/   # Zephyr SDK（arm-zephyr-eabi-gcc）
      nrfutil/bin/      # nrfutil
      usr/local/bin/    # west, cmake, python3.12 等
```

## 三、编译

所有编译通过项目根目录的 `Makefile` 执行：

```bash
# 编译 blinky sample（验证工具链）
make build-blinky

# 编译 CDC ACM sample（验证 USB 串口）
make build-cdc
```

Makefile 内部使用 `nrfutil sdk-manager toolchain launch` 注入环境，
并设置 `ZEPHYR_BASE` 让 west 找到 NCS workspace：

```bash
ZEPHYR_BASE=~/ncs/v3.4.0/zephyr nrfutil sdk-manager toolchain launch --ncs-version v3.4.0 -- \
  west build -b nrf52840dongle <source> -d <build_dir> --no-sysbuild
```

> **为什么需要 `ZEPHYR_BASE`？**
> 本项目不在 NCS 文件夹层级内，west 无法自动发现 workspace。
> `nrfutil sdk-manager toolchain launch` 不设置 `ZEPHYR_BASE`，
> 需要手动指定。参见
> `sdk-nrf/doc/nrf/app_dev/create_application.rst` 中关于 `ZEPHYR_BASE`
> 的说明。

### 手动编译（不用 Makefile）

```bash
ZEPHYR_BASE=~/ncs/v3.4.0/zephyr nrfutil sdk-manager toolchain launch --ncs-version v3.4.0 -- \
  west build -b nrf52840dongle ~/ncs/v3.4.0/zephyr/samples/basic/blinky \
  -d build-blinky --no-sysbuild
```

### 交互式开发环境

如果需要连续执行多个 west 命令，可以启动隔离 shell：

```bash
ZEPHYR_BASE=~/ncs/v3.4.0/zephyr nrfutil sdk-manager toolchain launch --ncs-version v3.4.0 --shell
```

这会启动一个配置好所有环境变量的子 shell（含 `ZEPHYR_BASE`），
退出时环境自动清理。

## 四、烧录

### 4.1 进入 DFU 模式

nRF52840 Dongle 通过 USB DFU 模式烧录（不需要 J-Link）：

1. 将 Dongle 插入 USB 口
2. 按 Dongle 侧面的 **Reset** 按键（短暂按下）
3. Dongle 进入 DFU 模式，红色 LED 呼吸闪烁
4. 验证：`nrfutil device list` 显示 "Open DFU Bootloader"

### 4.2 烧录流程

nRF52840 Dongle 使用 Nordic secure DFU 协议，命令行**不能直接烧录
.hex 或 .elf 文件**，需要两步：

1. 从 .hex 生成 DFU zip 包（SdfuZip 格式）
2. 用 `nrfutil device program` 烧录 zip 包

> nRF Connect Desktop 的 Programmer app 可以直接烧 ELF/HEX，
> 因为 GUI 内部自动完成 HEX->DFU zip 转换。命令行无此自动化。

```bash
# 通过 Makefile（自动完成两步）
make flash-blinky
make flash-cdc

# 手动烧录（两步）
# 步骤 1：生成 DFU zip 包
nrfutil nrf5sdk-tools pkg generate \
  --application build-blinky/zephyr/zephyr.hex \
  --application-version 1 \
  --hw-version 52 \
  --sd-req 0x00 \
  build-blinky/zephyr/zephyr_dfu.zip

# 步骤 2：烧录 DFU zip 包
nrfutil device program --firmware build-blinky/zephyr/zephyr_dfu.zip --traits nordicDfu
```

> **注意**：`nrf5sdk-tools` 子命令需要单独安装（见 2.2 节）。
> 它内置了旧版 nrfutil v6.1.7 的 DFU 包生成功能。

### 4.3 查看连接的设备

```bash
make devices
# 或
nrfutil device list
```

### 4.4 多设备场景

如果连接了多个 nrfutil 支持的设备（如 STLink + Dongle），
需要用 `--traits` 或 `--serial-number` 指定目标：

```bash
# 按 traits 过滤
nrfutil device program --firmware xxx.zip --traits nordicDfu

# 按序列号指定
nrfutil device program --firmware xxx.zip --serial-number E0AE6F5055F5
```

### 4.5 固件格式与烧录方法对照

nrfutil v8.x 根据固件文件格式和设备 traits 自动选择烧录方法：

| 固件格式 | 扩展名 | 烧录方法 | 所需设备 traits |
|---------|--------|---------|---------------|
| IntelHex | .hex | SEGGER J-LINK | jlink |
| IntelHex | .hex | MCUboot serial recovery | mcuBoot |
| SdfuZip | .zip | Nordic secure DFU | nordicDfu |

nRF52840 Dongle 在 DFU 模式下的 traits 为 `nordicDfu`，
因此需要 SdfuZip (.zip) 格式的固件。

## 五、关键参数说明

| 参数 | 值 | 说明 |
|------|-----|------|
| Board 名字 | `nrf52840dongle` | 不是 `nrf52840dongle_nrf52840` |
| `ZEPHYR_BASE` | `~/ncs/v3.4.0/zephyr` | 本项目不在 NCS workspace 内，需手动设置（Makefile 已处理） |
| `--no-sysbuild` | - | 简单应用不需要 sysbuild（多镜像） |
| `ZEPHYR_TOOLCHAIN_VARIANT` | `zephyr/gnu` | 由 launch 自动设置 |
| `ZEPHYR_SDK_INSTALL_DIR` | `opt/zephyr-sdk` | 由 launch 自动设置 |
| 编译器 | `arm-zephyr-eabi-gcc 14.3.0` | 不是系统的 arm-none-eabi-gcc |

## 六、常见问题

### 6.1 No Nordic device found

Dongle 未插入或未进入 DFU 模式。按 Reset 键重新进入 DFU。

### 6.2 Missing --traits, --remote-jlink or --serial-number

连接了多个设备。加 `--traits nordicDfu` 指定 Dongle。

### 6.3 libpython3.12.so / libyaml-0.so not found

环境变量未正确设置。确保通过 `nrfutil sdk-manager toolchain launch`
执行命令，而不是直接运行 west。

### 6.4 No board named 'nrf52840dongle_nrf52840' found

Board 名字是 `nrf52840dongle`，不带 `_nrf52840` 后缀。

### 6.5 ZEPHYR_TOOLCHAIN_VARIANT not set

需要通过 `nrfutil sdk-manager toolchain launch` 注入环境，
或手动设置 `ZEPHYR_TOOLCHAIN_VARIANT=zephyr/gnu` 和
`ZEPHYR_SDK_INSTALL_DIR`。

### 6.6 invalid Zip archive: Could not find EOCD

试图直接烧录 .hex 文件到 Dongle（nordicDfu trait）。Dongle 需要
SdfuZip (.zip) 格式。先用 `nrfutil nrf5sdk-tools pkg generate` 生成
DFU zip 包，再烧录（见 4.2 节）。

### 6.7 No devices with requested trait(s) found

Dongle 未在 DFU 模式。按 Reset 键进入 DFU 模式，然后用
`nrfutil device list` 确认显示 "Open DFU Bootloader"。

### 6.8 west: unknown command "build"

未设置 `ZEPHYR_BASE`，west 找不到 NCS workspace 无法加载扩展命令。
确保通过 Makefile 编译（已自动设置），或手动编译时添加
`ZEPHYR_BASE=~/ncs/v3.4.0/zephyr` 前缀。

## 七、迁移到新机器

1. 安装 nrfutil 到 `~/.local/bin/`
2. `nrfutil install device sdk-manager nrf5sdk-tools`
3. `nrfutil sdk-manager install v3.4.0`
4. 克隆本项目
5. `make build-blinky` 验证编译
6. 将 Dongle 进入 DFU 模式，`make flash-blinky` 验证烧录

项目根目录的 `Makefile` 包含所有编译配置，无需修改 shell 配置。
