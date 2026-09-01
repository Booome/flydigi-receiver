# COMPONENT_NAME must be "main": the SDK's out-of-tree flow registers
# FBB_PROJECT_COMPONENT_NAME (default "main") into RAM_COMPONENT, and
# build_component() only links components present in that list.
set(COMPONENT_NAME "main")

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
)

set(PRIVATE_HEADER)

set(PUBLIC_HEADER
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(PRIVATE_DEFINES)

set(PUBLIC_DEFINES)

set(COMPONENT_CCFLAGS)

set(WHOLE_LINK true)

set(APP_TARGETS
"ws63-liteos-app"
)

set(BUILD_AS_OBJ false)

set(MAIN_COMPONENT false)
true

build_component()
