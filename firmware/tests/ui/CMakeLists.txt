cmake_minimum_required(VERSION 3.20)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nokia_ui_tests)

target_sources(app PRIVATE
    src/test_main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../app/src/ui_framework.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../app/src
)
