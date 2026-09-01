set(COMPONENT_NAME "flydigi_decoy")

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/decoy_server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/decoy_send_hook.c
    ${CMAKE_SOURCE_DIR}/src/bs21_util.c
)

set(PUBLIC_HEADER ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)

set(PRIVATE_HEADER)

set(PRIVATE_DEFINES)

set(PUBLIC_DEFINES)

set(COMPONENT_PUBLIC_CCFLAGS)

set(WHOLE_LINK true)

set(MAIN_COMPONENT false)

build_component()
