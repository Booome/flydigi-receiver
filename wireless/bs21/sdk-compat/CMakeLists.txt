set(COMPONENT_NAME "sdk_compat")

if("CONFIG_BT_SLE_ONLY" IN_LIST DEFINES)
    set(SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/ble_stub.c
    )
else()
    set(SOURCES)
endif()

set(PUBLIC_HEADER)

set(PRIVATE_HEADER)

set(PRIVATE_DEFINES)

set(PUBLIC_DEFINES)

set(COMPONENT_PUBLIC_CCFLAGS)

set(COMPONENT_CCFLAGS)

set(WHOLE_LINK
true
)

set(MAIN_COMPONENT
false
)

build_component()
