cmake_minimum_required(VERSION 3.20)

# Disable fatal Kconfig warnings (Zephyr 4.x)
set(KCONFIG_WARNINGS_AS_ERRORS OFF CACHE BOOL "Disable fatal Kconfig warnings")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nokia_ui)

target_sources(app PRIVATE
    src/main.c
    src/lcd_fb.c
)

