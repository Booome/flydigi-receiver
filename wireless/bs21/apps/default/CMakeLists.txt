set(COMPONENT_NAME "default")

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
)

set(PUBLIC_HEADER
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(PRIVATE_HEADER)

set(PRIVATE_DEFINES)

set(PUBLIC_DEFINES)

set(COMPONENT_PUBLIC_CCFLAGS)

set(COMPONENT_CCFLAGS "-Wno-unused-parameter")

set(WHOLE_LINK
true
)

set(MAIN_COMPONENT
false
)

build_component()
