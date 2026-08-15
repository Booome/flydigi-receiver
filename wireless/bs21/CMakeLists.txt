cmake_minimum_required(VERSION 3.14.1)
set(CMAKE_SYSTEM_NAME "Generic")

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

set(ENV{PYTHONDONTWRITEBYTECODE} "1")

set(CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/sdk-config.cmake")
execute_process(
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen-config.py
            ${SDK_ROOT} ${BS21_TARGET} ${CONFIG_FILE}
    RESULT_VARIABLE gen_result)
if(NOT gen_result EQUAL 0)
    message(FATAL_ERROR "gen-config.py failed: ${gen_result}")
endif()
include(${CONFIG_FILE})

include(${SDK_ROOT}/build/cmake/build_function.cmake)
include(${SDK_ROOT}/build/cmake/global_variable.cmake)
include(${SDK_ROOT}/build/cmake/build_script.cmake)
include(${SDK_ROOT}/build/cmake/build_command.cmake)
include(${SDK_ROOT}/build/cmake/build_hso_database.cmake)
include(${SDK_ROOT}/build/cmake/build_component.cmake)
include(${SDK_ROOT}/build/cmake/build_sdk.cmake)

project(${CHIP}_CFBB C ASM CXX)

set(TARGET_COMPONENT "${RAM_COMPONENT}" "${ROM_COMPONENT}")
set(TARGET_NAME ${BIN_NAME})
file(WRITE ${PROJECT_BINARY_DIR}/temp/__null___.c "int __null___(void) {return 0;}")
add_executable(${BIN_NAME} ${PROJECT_BINARY_DIR}/temp/__null___.c)
set_target_properties(${BIN_NAME} PROPERTIES RUNTIME_OUTPUT_NAME ${BIN_NAME}.elf)
target_compile_options(${BIN_NAME} PRIVATE "${CCFLAGS}")

string(REPLACE "-" "_" BS21_TARGET_CFG ${BS21_TARGET})
execute_process(
    COMMAND ${Python3_EXECUTABLE} ${SDK_ROOT}/build/script/usr_config.py
            --command savemenuconfig --chip ${CHIP} --core ${CORE}
            --target ${BS21_TARGET} --output ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${SDK_ROOT}
    RESULT_VARIABLE mconfig_result)
if(NOT mconfig_result EQUAL 0)
    message(FATAL_ERROR "mconfig failed")
endif()

set(KCONFIG_PATH "${SDK_ROOT}/build/config/target_config/${CHIP}/menuconfig/${CORE}/${BS21_TARGET_CFG}.config")
if(EXISTS ${KCONFIG_PATH})
    KCONFIG_GET_PARAMS(${KCONFIG_PATH})
    set(USE_KCONFIG True)
endif()

add_subdirectory(${SDK_ROOT}/application/bs21/standard ${CMAKE_BINARY_DIR}/sdk/standard)
add_subdirectory(${SDK_ROOT}/kernel ${CMAKE_BINARY_DIR}/sdk/kernel)
add_subdirectory(${SDK_ROOT}/drivers ${CMAKE_BINARY_DIR}/sdk/drivers)
add_subdirectory(${SDK_ROOT}/middleware ${CMAKE_BINARY_DIR}/sdk/middleware)
add_subdirectory(${SDK_ROOT}/open_source ${CMAKE_BINARY_DIR}/sdk/open_source)
add_subdirectory(app)
add_subdirectory(sdk-compat)

add_custom_target(package ALL
    COMMAND ${Python3_EXECUTABLE} ${SDK_ROOT}/tools/pkg/packet.py
            ${CHIP} ${BS21_TARGET} NO_BOOT_BACKUP ${SECTOR_CFG}
    WORKING_DIRECTORY ${SDK_ROOT}
    DEPENDS ${BIN_NAME})
