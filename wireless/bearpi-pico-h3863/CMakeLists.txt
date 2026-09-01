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
# BIN_NAME 必须等于 SDK 打包管线硬编码的目标名（packet.py / params_and_bin_sign.py 依赖
# <target>_rom.bin 与 <target>-sign.bin），不能自定义，否则 fwpkg 生成会被跳过。
set(BIN_NAME "ws63-liteos-app")
set(CORE "acore")
set(BOARD "evb")
set(ARCH "riscv31")
set(ARCH_FAMILY "riscv")
set(BUILD_TARGET_NAME "ws63_liteos_app")
set(BUILD_TYPE "Debug")
set(APPLICATION "application")

# SDK 默认 ccflags 含 -Werror 及 -DBOARD_ASIC 等关键选项，只能追加不能整体覆盖， 否则 wifi
# 组件（romrepo/basic.cmake 检查 `-DBOARD_ASIC IN_LIST CCFLAGS`）会选错 ASIC/FPGA 头文件路径。
if(DEFINED CCFLAGS)
  list(
    APPEND
    CCFLAGS
    "-Wno-unused-parameter"
    "-Wno-unused-variable"
    "-Wno-unused-const-variable"
  )
endif()

# ws63 ASIC：SDK config.py 把 BOARD_ASIC 放在 ccflags，但 wifi 组件 master/basic.cmake 用
# `BOARD_ASIC IN_LIST DEFINES` 判断，需同步进 DEFINES。
if(CHIP STREQUAL "ws63")
  list(APPEND DEFINES "BOARD_ASIC")
endif()

# 引入 SDK 的 out-of-tree 入口
include("${FBB_SDK_DIR}/build/cmake/project.cmake")

# 声明项目
project(${CHIP}_CFBB C ASM)
