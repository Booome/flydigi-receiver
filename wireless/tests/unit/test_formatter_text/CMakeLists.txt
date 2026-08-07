cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_formatter_text)

target_sources(app PRIVATE
    src/test_formatter_text.c
    ../../../firmware/src/formatter_text.c
)
target_include_directories(app PRIVATE ../../../firmware/src)
