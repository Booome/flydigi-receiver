cmake_minimum_required(VERSION 3.16)

set(APP_NAME bs21_receiver)
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

file(GLOB_RECURSE APP_SOURCES ${SRC_DIR}/*.c)

project(${APP_NAME})
add_executable(${APP_NAME} ${APP_SOURCES})