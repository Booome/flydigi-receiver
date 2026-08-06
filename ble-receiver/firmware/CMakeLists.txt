cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ble_receiver)

# Devicetree overlay for dual CDC ACM
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/app.overlay CACHE FILEPATH "Devicetree overlay" FORCE)

# Reuse Zephyr sample USB device initialization code
include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)

target_sources(app PRIVATE
    src/main.c
    src/formatter_text.c
    src/formatter_binary.c
)

target_sources_ifdef(CONFIG_OUTPUT_CDC app PRIVATE src/output_cdc.c)
target_sources_ifdef(CONFIG_OUTPUT_UART app PRIVATE src/output_uart.c)

target_include_directories(app PRIVATE src)
