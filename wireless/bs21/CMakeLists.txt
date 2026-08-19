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
if(NOT DEFINED BS21_APP)
    set(BS21_APP "default")
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(ENV{PYTHONDONTWRITEBYTECODE} "1")
# build-time scripts are launched by make and do not inherit the env above,
# so wrap the interpreter to keep the SDK tree free of __pycache__ writes.
set(Python3_EXECUTABLE "${CMAKE_COMMAND}" "-E" "env" "PYTHONDONTWRITEBYTECODE=1" "${Python3_EXECUTABLE}")

set(CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/sdk-config.cmake")
execute_process(
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen-config.py
            ${SDK_ROOT} ${BS21_TARGET} ${CONFIG_FILE} ${BS21_APP}
    RESULT_VARIABLE gen_result)
if(NOT gen_result EQUAL 0)
    message(FATAL_ERROR "gen-config.py failed: ${gen_result}")
endif()
include(${CONFIG_FILE})

# global compile flags for all apps
string(REPLACE ";" " " _CCFLAGS "${CCFLAGS}")
if(NOT "${_CCFLAGS}" MATCHES "Wno-unused-parameter")
    set(CCFLAGS "${CCFLAGS};-Wno-unused-parameter" CACHE INTERNAL "" FORCE)
endif()

set(ROOT_DIR "${SDK_ROOT}")
set(CMAKE_DIR "${SDK_ROOT}/build/cmake")
set(BIN_DIR "${SDK_ROOT}/interim_binary")
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake")
set(PKG_TARGET_NAME ${BS21_TARGET})
string(REPLACE "-" "_" BUILD_TARGET_NAME ${BS21_TARGET})
list(APPEND RAM_COMPONENT rom_callback)
list(APPEND DEFINES _PRE_FEATURE_VENEER_ROM)
set(BUILD_ROM_CALLBACK False)

include(${SDK_ROOT}/build/cmake/build_function.cmake)
include(${SDK_ROOT}/build/cmake/global_variable.cmake)
include(${SDK_ROOT}/build/cmake/build_script.cmake)
include(${SDK_ROOT}/build/cmake/build_command.cmake)
include(${SDK_ROOT}/build/cmake/build_hso_database.cmake)
include(${SDK_ROOT}/build/cmake/build_component.cmake)
include(${SDK_ROOT}/build/cmake/build_sdk.cmake)

project(${CHIP}_CFBB C ASM CXX)
set(MODULE_NAME "pf")
set(AUTO_DEF_FILE_ID TRUE)
set(AUTO_DEF_MODULE_ID TRUE)
set(CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES "")
file(REMOVE ${CMAKE_BINARY_DIR}/auto_gen_libfunc.lds)

set(TARGET_COMPONENT "${RAM_COMPONENT}" "${ROM_COMPONENT}")
set(TARGET_NAME ${BIN_NAME})
file(WRITE ${PROJECT_BINARY_DIR}/temp/__null___.c "int __null___(void) {return 0;}")
add_executable(${BIN_NAME} ${PROJECT_BINARY_DIR}/temp/__null___.c)
set_target_properties(${BIN_NAME} PROPERTIES RUNTIME_OUTPUT_NAME ${BIN_NAME}.elf)
target_compile_options(${BIN_NAME} PRIVATE "${CCFLAGS}")

include(${SDK_ROOT}/build/cmake/build_rom_callback.cmake)
if(${BUILD_ROM_CALLBACK})
    build_rom_callback()
endif()

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
    # bs2x_sample_config.cmake normally appends these, but its Kconfig-derived
    # variables are not visible here; ensure the log region is reserved.
    if(DEFINED CONFIG_SUPPORT_LOG_THREAD)
        list(APPEND DEFINES CONFIG_SUPPORT_LOG_THREAD)
    endif()
endif()

add_subdirectory(${SDK_ROOT}/application/bs21/standard ${CMAKE_BINARY_DIR}/sdk/standard)
add_subdirectory(${SDK_ROOT}/kernel ${CMAKE_BINARY_DIR}/sdk/kernel)
add_subdirectory(${SDK_ROOT}/drivers ${CMAKE_BINARY_DIR}/sdk/drivers)
add_subdirectory(${SDK_ROOT}/middleware ${CMAKE_BINARY_DIR}/sdk/middleware)
add_subdirectory(${SDK_ROOT}/open_source ${CMAKE_BINARY_DIR}/sdk/open_source)
add_subdirectory(apps/${BS21_APP})
add_subdirectory(sdk-compat)

add_path_with_alias_if_exist(${SDK_ROOT}/protocol sdk/protocol)
add_path_with_alias_if_exist(${SDK_ROOT}/test sdk/test)
add_path_with_alias_if_exist(${SDK_ROOT}/include sdk/include)
add_path_with_alias_if_exist(${SDK_ROOT}/vendor sdk/vendor)

include(${SDK_ROOT}/build/cmake/open_source.cmake)
include(${SDK_ROOT}/build/cmake/build_linker.cmake)

if (NOT DEFINED ROM_COMPONENT)
add_custom_target(GENERAT_BIN ALL
    COMMAND ${CMAKE_OBJCOPY} --gap-fill 0xFF -O binary -R .fls_loader_ram -R .logstr -R .ARM -R .ARM ${BIN_NAME}.elf ${BIN_NAME}.bin
    COMMENT "post_build:gen bin file"
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    DEPENDS ${TARGET_NAME}
)
else()
add_custom_target(GENERAT_BIN ALL
    COMMAND ${CMAKE_OBJCOPY} --gap-fill 0xFF -O binary -R .logstr -R .ARM.exidx -R .ARM.extab -R .*_romtext ${TARGET_NAME}.elf ${TARGET_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} --gap-fill 0xFF -O binary -j .*_romtext ${TARGET_NAME}.elf ${TARGET_NAME}_rom.bin
    COMMENT "post_build:gen rom and ram bin file"
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    DEPENDS ${TARGET_NAME}
)
endif()

include(${SDK_ROOT}/build/cmake/build_ssb.cmake)
include(${SDK_ROOT}/build/cmake/build_elf_info.cmake)
set(PACKAGE_DEPENDS ${BIN_NAME} GENERAT_NVBIN GENERAT_PARTITION
        COPY_FLASHBOOT_BIN COPY_LOADERBOOT_BIN)
if (EXISTS ${SDK_ROOT}/build/config/target_config/${CHIP}/sign_config/${BUILD_TARGET_NAME}.cfg)
    set(BS21_SIGN_DIR "${SDK_ROOT}/output/${CHIP}/${CORE}/${PKG_TARGET_NAME}")
    if(TARGET GENERAT_ROM_PATCH)
        set(SIGNBIN_DEPS GENERAT_BIN GENERAT_ROM_PATCH)
    else()
        set(SIGNBIN_DEPS GENERAT_BIN)
    endif()
    add_custom_target(GENERAT_SIGNBIN ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${BS21_SIGN_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_BINARY_DIR}/${BIN_NAME}.bin ${BS21_SIGN_DIR}/${BIN_NAME}.bin
        COMMAND ${SIGN_TOOL} 0 ${SDK_ROOT}/build/config/target_config/${CHIP}/sign_config/${BUILD_TARGET_NAME}.cfg
        COMMENT "sign file:gen boot sign file"
        WORKING_DIRECTORY ${BS21_SIGN_DIR}
        DEPENDS ${SIGNBIN_DEPS}
    )
    list(APPEND PACKAGE_DEPENDS GENERAT_SIGNBIN)
    if(${GEN_SEC_BIN} STREQUAL "True")
        if (EXISTS ${COMPILER_ROOT}/lib/sec/${IMAGE_VERSION}/libsec_image.so)
            execute_process(COMMAND ${CMAKE_COMMAND} -E copy
                ${COMPILER_ROOT}/lib/sec/${IMAGE_VERSION}/libsec_image.so
                ${COMPILER_ROOT}/lib/sec/libsec_image.so)
        endif()
        add_custom_target(GENERAT_SEC_IMAGE ALL
            COMMAND ${CMAKE_OBJCOPY} --enable_sec ${BS21_SIGN_DIR}/${BIN_NAME}_sign.bin
            COMMENT "enable sec image"
            WORKING_DIRECTORY ${COMPILER_ROOT}/bin
            DEPENDS GENERAT_SIGNBIN
        )
        list(APPEND PACKAGE_DEPENDS GENERAT_SEC_IMAGE)
    endif()
endif()
include(${SDK_ROOT}/build/cmake/build_nv_bin.cmake)
include(${SDK_ROOT}/build/cmake/build_partition_bin.cmake)
include(${SDK_ROOT}/build/cmake/build_boot_bin_cp.cmake)

create_hso_db()
generate_project_file()

add_custom_target(package ALL
    COMMAND ${Python3_EXECUTABLE} ${SDK_ROOT}/tools/pkg/packet.py
            ${CHIP} ${BS21_TARGET} NO_BOOT_BACKUP ${SECTOR_CFG}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SDK_ROOT}/output/${CHIP}/fwpkg/${BS21_TARGET} ${PROJECT_BINARY_DIR}
    # packet.py hardcodes a write into a tracked SDK file; restore it to keep the SDK tree clean
    COMMAND git -C ${SDK_ROOT} checkout -- tools/pkg/fwpkg/${CHIP}/${CHIP}_all.fwpkg
    # gen-config.py (via SDK enviroment.rm_pyc) removes tracked pycache; restore it
    COMMAND git -C ${SDK_ROOT} checkout -- build/config/target_config/${CHIP}/fota/__pycache__/fota_format_st.cpython-39.pyc
    WORKING_DIRECTORY ${SDK_ROOT}
    DEPENDS ${PACKAGE_DEPENDS})
