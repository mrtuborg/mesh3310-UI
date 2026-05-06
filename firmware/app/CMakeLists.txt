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

target_sources_ifdef(CONFIG_NOKIA_BACKEND_SH1107_I2C app PRIVATE
    src/display.c
    src/display_transform.c
    src/display_sh1107.c
)

