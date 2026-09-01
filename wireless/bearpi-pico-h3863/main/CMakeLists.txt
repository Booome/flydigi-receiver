# The SDK's out-of-tree flow requires this file at
# ${CMAKE_SOURCE_DIR}/main/CMakeLists.txt and registers the component named
# FBB_PROJECT_COMPONENT_NAME (default "main") into RAM_COMPONENT. Select which
# app to build via the FBB_APP env var (set by scripts/build.sh), mirroring
# the ai-bs21-32s-kit apps/<app> layout.
if(NOT DEFINED ENV{FBB_APP})
    set(FBB_APP "default")
else()
    set(FBB_APP "$ENV{FBB_APP}")
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../apps/${FBB_APP}/CMakeLists.txt")
    message(FATAL_ERROR
        "unknown app '${FBB_APP}' (no apps/${FBB_APP}/CMakeLists.txt); "
        "set FBB_APP to one of the apps/ directories")
endif()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../apps/${FBB_APP}"
                 "${CMAKE_BINARY_DIR}/_fbb_app_${FBB_APP}")
