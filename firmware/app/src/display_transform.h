#pragma once

#include <stdint.h>

/*
 * Fill buf[NOKIA_LCD_WIDTH] with the transformed pixel data for the given
 * display page (0 .. NOKIA_LCD_PAGES-1), reading from nokia_fb and applying
 * CONFIG_NOKIA_DISPLAY_ROTATION and CONFIG_NOKIA_DISPLAY_VFLIP.
 */
void display_transform_page(int page, uint8_t *buf);
