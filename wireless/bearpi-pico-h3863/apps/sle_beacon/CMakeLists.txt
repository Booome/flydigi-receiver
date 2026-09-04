set(COMPONENT_NAME "main")

set(SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.c
            ${CMAKE_CURRENT_SOURCE_DIR}/sle_beacon.c
)

set(PUBLIC_HEADER ${CMAKE_CURRENT_SOURCE_DIR})

set(PRIVATE_HEADER)

set(PRIVATE_DEFINES)

set(PUBLIC_DEFINES)

set(COMPONENT_CCFLAGS)

set(WHOLE_LINK true)

set(APP_TARGETS "ws63-liteos-app")

set(BUILD_AS_OBJ false)

set(MAIN_COMPONENT true)

build_component()