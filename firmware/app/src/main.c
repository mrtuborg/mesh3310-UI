#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdint.h>
#include <string.h>

/* Nokia 5110 framebuffer:
 * 84 x 48 pixels
 * 1 bit per pixel
 * Stored as 6 rows of 84 bytes (PCD8544 format)
 */
#define LCD_WIDTH   84
#define LCD_HEIGHT  48
#define LCD_PAGES   (LCD_HEIGHT / 8)

/* Minimal 5x7 glyph set used by the UI. Each glyph is 5 columns, LSB = top row.
 * Only characters needed for the main screen are implemented here.
 */
static const uint8_t glyph_space[5] = {0x00,0x00,0x00,0x00,0x00};
static const uint8_t glyph_L[5]     = {0x7F,0x40,0x40,0x40,0x40};
static const uint8_t glyph_M[5]     = {0x7F,0x02,0x04,0x02,0x7F};
static const uint8_t glyph_E[5]     = {0x7F,0x49,0x49,0x49,0x41};
static const uint8_t glyph_N[5]     = {0x7F,0x04,0x08,0x10,0x7F};
static const uint8_t glyph_U[5]     = {0x3F,0x40,0x40,0x40,0x3F};
static const uint8_t glyph_A[5]     = {0x7E,0x11,0x11,0x11,0x7E};
static const uint8_t glyph_S[5]     = {0x46,0x49,0x49,0x49,0x31};
static const uint8_t glyph_O[5]     = {0x3E,0x41,0x41,0x41,0x3E};
static const uint8_t glyph_P[5]     = {0x7F,0x09,0x09,0x09,0x06};
static const uint8_t glyph_R[5]     = {0x7F,0x09,0x19,0x29,0x46};
static const uint8_t glyph_T[5]     = {0x01,0x01,0x7F,0x01,0x01};

static const uint8_t glyph_0[5] = {0x3E,0x51,0x49,0x45,0x3E};
static const uint8_t glyph_1[5] = {0x00,0x42,0x7F,0x40,0x00};
static const uint8_t glyph_2[5] = {0x42,0x61,0x51,0x49,0x46};
static const uint8_t glyph_3[5] = {0x21,0x41,0x45,0x4B,0x31};
static const uint8_t glyph_4[5] = {0x18,0x14,0x12,0x7F,0x10};
static const uint8_t glyph_5[5] = {0x27,0x45,0x45,0x45,0x39};
static const uint8_t glyph_6[5] = {0x3C,0x4A,0x49,0x49,0x30};
static const uint8_t glyph_7[5] = {0x01,0x71,0x09,0x05,0x03};
static const uint8_t glyph_8[5] = {0x36,0x49,0x49,0x49,0x36};
static const uint8_t glyph_9[5] = {0x06,0x49,0x49,0x29,0x1E};
static const uint8_t glyph_colon[5] = {0x00,0x36,0x36,0x00,0x00};

static const uint8_t *font_lookup(char c)
{
    if (c >= 'a' && c <= 'z') {
        c -= ('a' - 'A');
    }

    switch (c) {
    case ' ': return glyph_space;
    case 'L': return glyph_L;
    case 'M': return glyph_M;
    case 'E': return glyph_E;
    case 'N': return glyph_N;
    case 'U': return glyph_U;
    case 'A': return glyph_A;
    case 'S': return glyph_S;
    case 'O': return glyph_O;
    case 'P': return glyph_P;
    case 'R': return glyph_R;
    case 'T': return glyph_T;
    case '0': return glyph_0;
    case '1': return glyph_1;
    case '2': return glyph_2;
    case '3': return glyph_3;
    case '4': return glyph_4;
    case '5': return glyph_5;
    case '6': return glyph_6;
    case '7': return glyph_7;
    case '8': return glyph_8;
    case '9': return glyph_9;
    case ':': return glyph_colon;
    default: return glyph_space;
    }
}



/* This symbol is IMPORTANT:
 * Renode / Python will read it directly from RAM later
 */
__attribute__((used))
uint8_t nokia_fb[LCD_PAGES][LCD_WIDTH];


static void fb_clear(void)
{
    memset(nokia_fb, 0x00, sizeof(nokia_fb));
}

static void fb_set_pixel(int x, int y)
{
    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT) {
        return;
    }

    int page = y / 8;
    int bit  = y % 8;

    nokia_fb[page][x] |= (1 << bit);
}

static void fb_draw_char(int x, int y, const uint8_t *glyph)
{
    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                fb_set_pixel(x + col, y + row);
            }
        }
    }
}

static void fb_draw_string(int x, int y, const char *s)
{
    for (size_t i = 0; s[i] != '\0'; i++) {
        const uint8_t *g = font_lookup(s[i]);
        fb_draw_char(x + (int)i * 6, y, g);
    }
}

static void fb_draw_char_scaled(int x, int y, const uint8_t *glyph, int scale)
{
    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                for (int sx = 0; sx < scale; sx++) {
                    for (int sy = 0; sy < scale; sy++) {
                        fb_set_pixel(x + col * scale + sx, y + row * scale + sy);
                    }
                }
            }
        }
    }
}

static void fb_draw_string_scaled(int x, int y, const char *s, int scale)
{
    for (size_t i = 0; s[i] != '\0'; i++) {
        const uint8_t *g = font_lookup(s[i]);
        fb_draw_char_scaled(x + (int)i * (5 * scale + scale), y, g, scale);
    }
}

static void draw_signal_bars(void)
{
    int base_x = 2;
    int base_y = 1;

    for (int i = 0; i < 5; i++) {
        int h = 2 + i * 2;
        for (int y = 0; y < h; y++) {
            fb_set_pixel(base_x + i * 3, base_y + (10 - y));
        }
    }
}

static void draw_battery_icon(void)
{
    int x = LCD_WIDTH - 14;
    int y = 1;

    // outline
    for (int i = 0; i < 10; i++) {
        fb_set_pixel(x + i, y);
        fb_set_pixel(x + i, y + 6);
    }
    for (int i = 0; i < 7; i++) {
        fb_set_pixel(x, y + i);
        fb_set_pixel(x + 9, y + i);
    }

    // terminal
    fb_set_pixel(x + 10, y + 2);
    fb_set_pixel(x + 10, y + 3);
}

static void draw_clock(void)
{
    // Large digits are simulated by spacing, not scaling
    fb_draw_string(26, 10, "12:34");
}

static void draw_operator(void)
{
    fb_draw_string(30, 26, "LORA");
}

static void draw_menu(void)
{
    fb_draw_string(30, 38, "MENU");
}

static void draw_idle_screen(void)
{
    fb_clear();

    draw_signal_bars();
    draw_battery_icon();
    draw_clock();
    draw_operator();
    draw_menu();
}


int main(void)
{
    printk("Mesh3310 UI demo starting...\n");

    while (1) {
        draw_idle_screen();
        k_sleep(K_SECONDS(1));
    }
}


