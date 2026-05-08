#include "display_transform.h"

#include <string.h>

#include "ui_framework.h"

void display_transform_page(int page, uint8_t *buf)
{
    if (page < 0 || page >= NOKIA_LCD_PAGES) {
        memset(buf, 0, NOKIA_LCD_WIDTH);
        return;
    }

#if CONFIG_NOKIA_DISPLAY_ROTATION == 0 && !CONFIG_NOKIA_DISPLAY_VFLIP
    memcpy(buf, nokia_fb[page], NOKIA_LCD_WIDTH);

#elif CONFIG_NOKIA_DISPLAY_ROTATION == 270
    /*
     * Optimised 270° rotation for square (128×128) display.
     * Output page P, column dx gets pixel from fb at:
     *   src_x = NOKIA_LCD_HEIGHT - 1 - (page*8 + bit)
     *   src_y = dx
     * Rewritten to avoid per-pixel function calls.
     */
    for (int dx = 0; dx < NOKIA_LCD_WIDTH; dx++) {
        uint8_t byte = 0;
        int src_y = dx;
        int src_page = src_y >> 3;
        int src_bit  = src_y & 7;
        uint8_t src_mask = (uint8_t)(1u << src_bit);

        for (int bit = 0; bit < 8; bit++) {
            int src_x = NOKIA_LCD_HEIGHT - 1 - (page * 8 + bit);
#if CONFIG_NOKIA_DISPLAY_VFLIP
            src_x = NOKIA_LCD_HEIGHT - 1 - src_x;
#endif
            if ((unsigned)src_x < (unsigned)NOKIA_LCD_WIDTH &&
                (nokia_fb[src_page][src_x] & src_mask)) {
                byte |= (uint8_t)(1u << bit);
            }
        }
        buf[dx] = byte;
    }

#elif CONFIG_NOKIA_DISPLAY_ROTATION == 90
    for (int dx = 0; dx < NOKIA_LCD_WIDTH; dx++) {
        uint8_t byte = 0;
        int src_y = NOKIA_LCD_HEIGHT - 1 - dx;
        int src_page = src_y >> 3;
        int src_bit  = src_y & 7;
        uint8_t src_mask = (uint8_t)(1u << src_bit);

        for (int bit = 0; bit < 8; bit++) {
            int src_x = page * 8 + bit;
#if CONFIG_NOKIA_DISPLAY_VFLIP
            src_x = NOKIA_LCD_HEIGHT - 1 - src_x;
#endif
            if ((unsigned)src_x < (unsigned)NOKIA_LCD_WIDTH &&
                (nokia_fb[src_page][src_x] & src_mask)) {
                byte |= (uint8_t)(1u << bit);
            }
        }
        buf[dx] = byte;
    }

#elif CONFIG_NOKIA_DISPLAY_ROTATION == 180
    for (int dx = 0; dx < NOKIA_LCD_WIDTH; dx++) {
        uint8_t byte = 0;
        for (int bit = 0; bit < 8; bit++) {
            int src_x = NOKIA_LCD_WIDTH - 1 - dx;
            int src_y = NOKIA_LCD_HEIGHT - 1 - (page * 8 + bit);
#if CONFIG_NOKIA_DISPLAY_VFLIP
            src_y = NOKIA_LCD_HEIGHT - 1 - src_y;
#endif
            int sp = src_y >> 3;
            if ((nokia_fb[sp][src_x] >> (src_y & 7)) & 1) {
                byte |= (uint8_t)(1u << bit);
            }
        }
        buf[dx] = byte;
    }

#else
    /* rotation == 0 with vflip */
    int src_page = NOKIA_LCD_PAGES - 1 - page;
    for (int dx = 0; dx < NOKIA_LCD_WIDTH; dx++) {
        uint8_t s = nokia_fb[src_page][dx];
        /* reverse bits within the byte */
        uint8_t r = 0;
        r |= (s & 0x01) << 7; r |= (s & 0x02) << 5;
        r |= (s & 0x04) << 3; r |= (s & 0x08) << 1;
        r |= (s & 0x10) >> 1; r |= (s & 0x20) >> 3;
        r |= (s & 0x40) >> 5; r |= (s & 0x80) >> 7;
        buf[dx] = r;
    }
#endif
}
