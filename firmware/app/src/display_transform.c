#include "display_transform.h"

#include <string.h>

#include "ui_framework.h"

static inline int fb_pixel(int x, int y)
{
    if (x < 0 || x >= NOKIA_LCD_WIDTH || y < 0 || y >= NOKIA_LCD_HEIGHT) {
        return 0;
    }
#if CONFIG_NOKIA_DISPLAY_VFLIP
    y = NOKIA_LCD_HEIGHT - 1 - y;
#endif
    return (nokia_fb[y / 8][x] >> (y % 8)) & 1;
}

void display_transform_page(int page, uint8_t *buf)
{
    if (page < 0 || page >= NOKIA_LCD_PAGES) {
        memset(buf, 0, NOKIA_LCD_WIDTH);
        return;
    }

#if CONFIG_NOKIA_DISPLAY_ROTATION == 90
    for (int dx = 0; dx < NOKIA_LCD_WIDTH; dx++) {
        uint8_t byte = 0;
        for (int bit = 0; bit < 8; bit++) {
            int dy = page * 8 + bit;
            byte |= fb_pixel(dy, NOKIA_LCD_HEIGHT - 1 - dx) << bit;
        }
        buf[dx] = byte;
    }
#elif CONFIG_NOKIA_DISPLAY_ROTATION == 180
    for (int dx = 0; dx < NOKIA_LCD_WIDTH; dx++) {
        uint8_t byte = 0;
        for (int bit = 0; bit < 8; bit++) {
            int dy = page * 8 + bit;
            byte |= fb_pixel(NOKIA_LCD_WIDTH - 1 - dx,
                             NOKIA_LCD_HEIGHT - 1 - dy) << bit;
        }
        buf[dx] = byte;
    }
#elif CONFIG_NOKIA_DISPLAY_ROTATION == 270
    for (int dx = 0; dx < NOKIA_LCD_WIDTH; dx++) {
        uint8_t byte = 0;
        for (int bit = 0; bit < 8; bit++) {
            int dy = page * 8 + bit;
            byte |= fb_pixel(NOKIA_LCD_HEIGHT - 1 - dy, dx) << bit;
        }
        buf[dx] = byte;
    }
#else
    memcpy(buf, nokia_fb[page], NOKIA_LCD_WIDTH);
#endif
}
