cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_formatter_binary)

target_sources(app PRIVATE
    src/test_formatter_binary.c
    ../../../firmware/src/formatter_binary.c
)
target_include_directories(app PRIVATE ../../../firmware/src)
