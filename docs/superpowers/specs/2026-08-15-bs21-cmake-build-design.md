# Ai-BS21_SDK 非侵入式构建设计（CMake 管理）

## 一、背景与目标

当前 `wireless/bs21/scripts/build.sh` 采用"侵入式 overlay"机制：把 `ble_stub.c`、
`demo.c` 复制进 SDK、sed 改 `config.py` 注入宏，构建完再 reset 恢复。这套机制不优雅，
且依赖 `trap cleanup EXIT` 才能保证 SDK 树恢复纯净，构建中断会留下脏文件。

**目标**：Ai-BS21_SDK 全程只读，构建入口是标准 CMake 流程
（`cmake -S ... -B ...` + `cmake --build ...`），无 Python 构建驱动脚本。SDK 仅以三种
只读方式被引用：

1. `include` 其 `build/cmake/*.cmake` 模块
2. `add_subdirectory`（绝对路径）其组件目录
3. `import` 其 Python 类（`TargetEnvironment`、`mconfig`）或直接命令行调用其脚本
   （`usr_config.py`、`packet.py`）

## 二、关键调查结论

- **无官方外部源码机制**：SDK 组件发现是硬编码的 `add_subdirectory_if_exist(固定目录名)`，
  没有 Zephyr 式的 `APPLICATION_SOURCE_DIR`。
- **无法复用 CMakeBuilder**：`build_utils.root_path` 同时充当"SDK 定位"和"cmake source
  dir"，硬编码耦合。故只复用纯配置生成的 `TargetEnvironment`。
- **`application/` 下 RAM_COMPONENT 组件仅 3 个**：`standard_porting`（必须）、`demo`、
  `samples`（均要排除）。因此可绕过 `application/CMakeLists.txt`，直接 add
  `application/bs21/standard`。
- **`mconfig.h` 编译必须**：`build_component.cmake` 用 `-include${PROJECT_BINARY_DIR}/mconfig.h`
  强制引入 `CONFIG_XXX` 宏。`usr_config.py` 有 argparse 入口，可命令行调
  `savemenuconfig`（`load_config()` + `write_autoconf()`，非交互）。
- **`packet.py` 签名**：`packet.py <chip> <target> <defines逗号分隔> <sector_cfg>`。

## 三、目录结构

```
wireless/bs21/
├── CMakeLists.txt          # 顶层入口（cmake -S 指向这里）
├── app/                    # 我们的应用组件
│   ├── CMakeLists.txt      # build_component() 注册，显式声明 SOURCES/PUBLIC_HEADER
│   └── main.c              # 应用入口，app_run(app_entry) 注册
├── sdk-compat/             # SDK 兼容补丁组件
│   ├── CMakeLists.txt
│   └── ble_stub.c          # 36 个 SLE-only 库缺失符号 stub
├── scripts/
│   ├── gen-config.py       # 参数生成器（execute_process 调用，复用 TargetEnvironment）
│   └── setup-sdk.sh        # 环境准备（chmod 工具链 + LiteOS lib symlink）
├── src/
│   └── controller_state.h  # 数据结构
└── .gitignore              # output/
```

删除：`overlay/`、`build.sh`。

## 四、构建流程（纯 CMake）

```bash
cmake -S wireless/bs21 -B output     # configure：execute_process 生成参数 + mconfig.h
cmake --build output                 # build：编译 + 打包（custom target）
```

`SDK_ROOT` 默认取环境变量 `AI_BS21_SDK_PATH`，否则 `~/.local/Ai-BS21_SDK`，可用
`-DSDK_ROOT=...` 覆盖。configure 零必需参数。

## 五、顶层 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.14.1)
set(CMAKE_SYSTEM_NAME "Generic")

# SDK_ROOT 默认值 + 可配置 target
if(NOT DEFINED SDK_ROOT)
    if(DEFINED ENV{AI_BS21_SDK_PATH})
        set(SDK_ROOT "$ENV{AI_BS21_SDK_PATH}")
    else()
        set(SDK_ROOT "$ENV{HOME}/.local/Ai-BS21_SDK")
    endif()
endif()
if(NOT DEFINED BS21_TARGET)
    set(BS21_TARGET "bs21-n1100-rcu")
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# 1. 生成 sdk-config.cmake（RAM_COMPONENT/DEFINES/CCFLAGS 等参数）
set(CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/sdk-config.cmake")
execute_process(
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen-config.py
            ${SDK_ROOT} ${BS21_TARGET} ${CONFIG_FILE}
    RESULT_VARIABLE gen_result)
if(NOT gen_result EQUAL 0)
    message(FATAL_ERROR "gen-config.py failed: ${gen_result}")
endif()
include(${CONFIG_FILE})

# 2. include SDK 模块
include(${SDK_ROOT}/build/cmake/build_function.cmake)
include(${SDK_ROOT}/build/cmake/global_variable.cmake)
include(${SDK_ROOT}/build/cmake/build_script.cmake)
include(${SDK_ROOT}/build/cmake/build_command.cmake)
include(${SDK_ROOT}/build/cmake/build_hso_database.cmake)
include(${SDK_ROOT}/build/cmake/build_component.cmake)
include(${SDK_ROOT}/build/cmake/build_sdk.cmake)

# 3. project + executable（复刻 SDK root CMakeLists 固定序列）
project(${CHIP}_CFBB C ASM CXX)
set(TARGET_COMPONENT "${RAM_COMPONENT}" "${ROM_COMPONENT}")
set(TARGET_NAME ${BIN_NAME})
file(WRITE ${PROJECT_BINARY_DIR}/temp/__null___.c "int __null___(void) {return 0;}")
add_executable(${BIN_NAME} ${PROJECT_BINARY_DIR}/temp/__null___.c)
set_target_properties(${BIN_NAME} PROPERTIES RUNTIME_OUTPUT_NAME ${BIN_NAME}.elf)
target_compile_options(${BIN_NAME} PRIVATE "${CCFLAGS}")

# 4. 生成 mconfig.h（autoconf header，编译必须）
execute_process(
    COMMAND ${Python3_EXECUTABLE} ${SDK_ROOT}/build/script/usr_config.py
            savemenuconfig ${CHIP} ${CORE} ${BS21_TARGET} ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${SDK_ROOT}
    RESULT_VARIABLE mconfig_result)
if(NOT mconfig_result EQUAL 0)
    message(FATAL_ERROR "mconfig failed")
endif()

# 5. KCONFIG + 组件引入
set(KCONFIG_PATH "${SDK_ROOT}/build/config/target_config/${CHIP}/menuconfig/${CORE}/${BS21_TARGET}.config")
if(EXISTS ${KCONFIG_PATH})
    KCONFIG_GET_PARAMS(${KCONFIG_PATH})
    set(USE_KCONFIG True)
endif()

add_path_if_exist(${SDK_ROOT}/application/bs21/standard)  # standard_porting（main.c + startup.S）
add_path_if_exist(${SDK_ROOT}/kernel)
add_path_if_exist(${SDK_ROOT}/drivers)
add_path_if_exist(${SDK_ROOT}/middleware)
add_path_if_exist(${SDK_ROOT}/open_source)
add_subdirectory(app)
add_subdirectory(sdk-compat)

# 6. 打包 fwpkg（build 阶段）
add_custom_target(package ALL
    COMMAND ${Python3_EXECUTABLE} ${SDK_ROOT}/tools/pkg/packet.py
            ${CHIP} ${BS21_TARGET} NO_BOOT_BACKUP ${SECTOR_CFG}
    WORKING_DIRECTORY ${SDK_ROOT}
    DEPENDS ${BIN_NAME})
```

## 六、参数生成器（scripts/gen-config.py）

被 `execute_process` 调用，复用 SDK 的 `TargetEnvironment`，输出 `set()` 语句到
`CONFIG_FILE`，供顶层 CMakeLists `include`。

```python
#!/usr/bin/env python3
import os, sys

SDK, TARGET, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
sys.path.append(os.path.join(SDK, 'build', 'script'))
sys.path.append(os.path.join(SDK, 'build', 'config'))
from enviroment import TargetEnvironment

env = TargetEnvironment(TARGET, extra_defines=['NO_BOOT_BACKUP'])
env.remove('ram_component', 'demo')
env.remove('ram_component', 'samples')
env.append('ram_component', 'app')
env.append('ram_component', 'sdk_compat')

# 复刻 deal_symbol_link：ROM_SYM_PATH 展开 + linkflags/defines 追加
if 'rom_sym_path' in env.config:
    env.config['rom_sym_path'] = env.config['rom_sym_path'].replace('<root>', SDK)
    if os.path.exists(env.config['rom_sym_path']):
        env.config['linkflags'].append('-Wl,--just-symbols=' + env.config['rom_sym_path'])
        env.config['linkflags'].extend(env.get('symlink_linkflags', cmake_type=False, default=[]))
        env.config['ccflags'].extend(env.get('symlink_ccflags', cmake_type=False, default=[]))
        env.config['defines'].append('ROM_SYMBOL_LINK')

with open(OUT, 'w') as f:
    for key in env.config:
        val = env.get(key)
        if val is None or val == '':
            continue
        f.write('set(%s "%s" CACHE INTERNAL "" FORCE)\n' % (key.upper(), val))
    f.write('set(CMAKE_TOOLCHAIN_FILE "%s")\n' % env.get_tool_chain())
```

`NO_BOOT_BACKUP` 修复 `bs21-n1100-rcu` 的 flash 布局 bug（partition 表 application @
0xb000，但链接脚本默认按 standard 布局 @ 0x15000）。

## 七、app 组件（app/）

```cmake
set(COMPONENT_NAME "app")
set(SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.c)
set(PUBLIC_HEADER ${CMAKE_CURRENT_SOURCE_DIR})
set(WHOLE_LINK true)   # 让 app_run 注册的 .zinitcall 符号不被 --gc-sections 裁掉
build_component()
```

`app/main.c` 初始内容（验证链路用，未来替换成 SLE 接收器）：

```c
#include "soc_osal.h"
#include "app_init.h"

static void *hello_task(const char *arg)
{
    (void)arg;
    while (1) {
        osal_printk("flydigi app running\r\n");
        osal_msleep(1000);
    }
    return NULL;
}

static void app_entry(void)
{
    osal_kthread_lock();
    osal_task *t = osal_kthread_create((osal_kthread_handler)hello_task, 0, "hello", 0x1000);
    if (t != NULL) osal_kthread_set_priority(t, 24);
    osal_kthread_unlock();
}

app_run(app_entry);
```

## 八、sdk-compat 组件（sdk-compat/）

`ble_stub.c` 补偿 Ai-BS21_SDK SLE-only 库（`libbth_sdk.a`）缺失的 36 个 `sapi_ble_*`
符号。独立成组件，未来 SDK 修复后直接删除此组件。

```cmake
set(COMPONENT_NAME "sdk_compat")
set(SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/ble_stub.c)
set(WHOLE_LINK true)
build_component()
```

## 九、验证标准

1. `cmake -S wireless/bs21 -B output && cmake --build output` 产出
   `output/bs21_all_in_one.fwpkg`，烧录后启动 + 串口打印 `flydigi app running`。
2. **SDK 只读**：构建前后 `git -C ~/.local/Ai-BS21_SDK status` 无变化。
3. 与当前固件等价：`_start` 在 `0x9010b300`，`NO_BOOT_BACKUP` 生效。

## 十、迁移步骤

1. 写顶层 `CMakeLists.txt` + `app/` + `sdk-compat/` + `scripts/gen-config.py`。
2. 新方案构建，验证标准 1/2/3 全过。
3. 删除旧 `build.sh` + `overlay/`。
4. 提交（保留 `setup-sdk.sh`）。

## 十一、风险点（实现时逐一验证）

1. **复刻 root CMakeLists 完整性**：`project`/`add_executable`/`TARGET_COMPONENT`/
   `KCONFIG_GET_PARAMS` 序列需与 SDK 一致，SDK 升级时需手动同步。
2. **execute_process 时机**：`gen-config.py` 生成 config.cmake 须在 `project()` 之前
   `include`，变量作用域与转义需验证（list 的 `;` 分隔、特殊字符）。
3. **mconfig 输出目录**：`usr_config.py savemenuconfig` 的 `output` 参数须指向我们的
   `CMAKE_BINARY_DIR`，确认 `mconfig.h` 落在 build 目录而非 SDK。
4. **packet.py 路径**：`add_custom_target` 的 `WORKING_DIRECTORY` 须为 SDK 根，确认
   packet.py 读 SDK 的 `interim_binary`/`output` 能正确拼出 fwpkg 到我们的 build 目录。
