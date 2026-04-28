cmake_minimum_required(VERSION 3.20)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nokia_ui)

target_sources(app PRIVATE
    src/main.c
    src/ui_framework.c
    src/screens.c
    src/game_snake.c
    src/applet_messages.c
)

