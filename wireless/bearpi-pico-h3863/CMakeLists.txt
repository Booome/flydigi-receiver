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
