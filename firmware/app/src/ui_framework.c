#include "ui_framework.h"
#include <string.h>
#include <stdint.h>

/* LCD geometry comes from ui_framework.h (NOKIA_LCD_*).
 * Local aliases keep the rest of this file readable. */
#define LCD_WIDTH   NOKIA_LCD_WIDTH
#define LCD_HEIGHT  NOKIA_LCD_HEIGHT
#define LCD_PAGES   NOKIA_LCD_PAGES

#define MENU_VISIBLE_ITEMS  3
#define MENU_ITEM_H         9           /* pixels per menu row         */

/* ------------------------------------------------------------------ */
/* 5×7 bitmap font, ASCII 0x20 – 0x7E (95 glyphs)                      */
/* Each byte is one column; bit 0 = top row, bit 6 = bottom row.       */
/* ------------------------------------------------------------------ */
static const uint8_t font_5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* 0x20 ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* 0x21 '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* 0x22 '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* 0x23 '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* 0x24 '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* 0x25 '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* 0x26 '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* 0x27 '\'' */
    {0x00,0x1C,0x22,0x41,0x00}, /* 0x28 '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* 0x29 ')' */
    {0x14,0x08,0x3E,0x08,0x14}, /* 0x2A '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* 0x2B '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* 0x2C ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* 0x2D '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* 0x2E '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* 0x2F '/' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0x30 '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* 0x31 '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* 0x32 '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* 0x33 '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* 0x34 '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* 0x35 '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 0x36 '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* 0x37 '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* 0x38 '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* 0x39 '9' */
    {0x00,0x36,0x36,0x00,0x00}, /* 0x3A ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* 0x3B ';' */
    {0x08,0x14,0x22,0x41,0x00}, /* 0x3C '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* 0x3D '=' */
    {0x00,0x41,0x22,0x14,0x08}, /* 0x3E '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* 0x3F '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* 0x40 '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 0x41 'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 0x42 'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 0x43 'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 0x44 'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 0x45 'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 0x46 'F' */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 0x47 'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 0x48 'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 0x49 'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 0x4A 'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 0x4B 'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 0x4C 'L' */
    {0x7F,0x02,0x04,0x02,0x7F}, /* 0x4D 'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 0x4E 'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 0x4F 'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 0x50 'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 0x51 'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 0x52 'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 0x53 'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 0x54 'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 0x55 'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 0x56 'V' */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 0x57 'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 0x58 'X' */
    {0x07,0x08,0x70,0x08,0x07}, /* 0x59 'Y' */
    {0x61,0x51,0x49,0x45,0x43}, /* 0x5A 'Z' */
    {0x00,0x7F,0x41,0x41,0x00}, /* 0x5B '[' */
    {0x02,0x04,0x08,0x10,0x20}, /* 0x5C '\\' */
    {0x00,0x41,0x41,0x7F,0x00}, /* 0x5D ']' */
    {0x04,0x02,0x01,0x02,0x04}, /* 0x5E '^' */
    {0x40,0x40,0x40,0x40,0x40}, /* 0x5F '_' */
    {0x00,0x01,0x02,0x04,0x00}, /* 0x60 '`' */
    {0x20,0x54,0x54,0x54,0x78}, /* 0x61 'a' */
    {0x7F,0x48,0x44,0x44,0x38}, /* 0x62 'b' */
    {0x38,0x44,0x44,0x44,0x20}, /* 0x63 'c' */
    {0x38,0x44,0x44,0x48,0x7F}, /* 0x64 'd' */
    {0x38,0x54,0x54,0x54,0x18}, /* 0x65 'e' */
    {0x08,0x7E,0x09,0x01,0x02}, /* 0x66 'f' */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 0x67 'g' */
    {0x7F,0x08,0x04,0x04,0x78}, /* 0x68 'h' */
    {0x00,0x44,0x7D,0x40,0x00}, /* 0x69 'i' */
    {0x20,0x40,0x44,0x3D,0x00}, /* 0x6A 'j' */
    {0x7F,0x10,0x28,0x44,0x00}, /* 0x6B 'k' */
    {0x00,0x41,0x7F,0x40,0x00}, /* 0x6C 'l' */
    {0x7C,0x04,0x18,0x04,0x78}, /* 0x6D 'm' */
    {0x7C,0x08,0x04,0x04,0x78}, /* 0x6E 'n' */
    {0x38,0x44,0x44,0x44,0x38}, /* 0x6F 'o' */
    {0x7C,0x14,0x14,0x14,0x08}, /* 0x70 'p' */
    {0x08,0x14,0x14,0x18,0x7C}, /* 0x71 'q' */
    {0x7C,0x08,0x04,0x04,0x08}, /* 0x72 'r' */
    {0x48,0x54,0x54,0x54,0x20}, /* 0x73 's' */
    {0x04,0x3F,0x44,0x40,0x20}, /* 0x74 't' */
    {0x3C,0x40,0x40,0x20,0x7C}, /* 0x75 'u' */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 0x76 'v' */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 0x77 'w' */
    {0x44,0x28,0x10,0x28,0x44}, /* 0x78 'x' */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 0x79 'y' */
    {0x44,0x64,0x54,0x4C,0x44}, /* 0x7A 'z' */
    {0x00,0x08,0x36,0x41,0x00}, /* 0x7B '{' */
    {0x00,0x00,0x7F,0x00,0x00}, /* 0x7C '|' */
    {0x00,0x41,0x36,0x08,0x00}, /* 0x7D '}' */
    {0x08,0x04,0x08,0x10,0x08}, /* 0x7E '~' */
};

static const uint8_t *font_get(char c)
{
    if ((uint8_t)c < 0x20u || (uint8_t)c > 0x7Eu) {
        c = '?';
    }
    return font_5x7[(uint8_t)(c - 0x20u)];
}

/* ------------------------------------------------------------------ */
/* Framebuffer                                                          */
/* Exported symbol: Renode / lcd_viewer.py reads it directly from RAM. */
/* ------------------------------------------------------------------ */
__attribute__((used))
uint8_t nokia_fb[LCD_PAGES][LCD_WIDTH];

static void fb_clear(void)
{
    memset(nokia_fb, 0x00, sizeof(nokia_fb));
}

static void fb_set_pixel(int x, int y)
{
    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT) return;
    nokia_fb[y / 8][x] |= (uint8_t)(1u << (y % 8));
}

static void fb_fill_rect(int x, int y, int w, int h)
{
    for (int px = x; px < x + w; px++) {
        for (int py = y; py < y + h; py++) {
            if (px >= 0 && px < LCD_WIDTH && py >= 0 && py < LCD_HEIGHT) {
                nokia_fb[py / 8][px] |= (uint8_t)(1u << (py % 8));
            }
        }
    }
}

static void fb_hline(int y)
{
    for (int x = 0; x < LCD_WIDTH; x++) {
        fb_set_pixel(x, y);
    }
}

/* Draw a single glyph. xor_mode=1 flips pixels (white text on black bg). */
static void fb_draw_glyph(int x, int y, const uint8_t *glyph, int xor_mode)
{
    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (!(bits & (1u << row))) continue;
            int px = x + col;
            int py = y + row;
            if (px < 0 || px >= LCD_WIDTH || py < 0 || py >= LCD_HEIGHT) continue;
            if (xor_mode) {
                nokia_fb[py / 8][px] ^= (uint8_t)(1u << (py % 8));
            } else {
                nokia_fb[py / 8][px] |= (uint8_t)(1u << (py % 8));
            }
        }
    }
}

static void fb_draw_glyph_scaled(int x, int y, const uint8_t *glyph, int scale)
{
    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (!(bits & (1u << row))) continue;
            for (int sx = 0; sx < scale; sx++) {
                for (int sy = 0; sy < scale; sy++) {
                    fb_set_pixel(x + col * scale + sx, y + row * scale + sy);
                }
            }
        }
    }
}

static void fb_draw_string(int x, int y, const char *s, int xor_mode)
{
    for (int i = 0; s[i]; i++) {
        fb_draw_glyph(x + i * 6, y, font_get(s[i]), xor_mode);
    }
}

static void fb_draw_string_centered(int y, const char *s)
{
    int len = (int)strlen(s);
    if (len > LCD_WIDTH / 6) len = LCD_WIDTH / 6;   /* guard against overflow */
    int x   = (LCD_WIDTH - len * 6) / 2;
    if (x < 0) x = 0;
    fb_draw_string(x, y, s, 0);
}

static void fb_draw_string_scaled(int x, int y, const char *s, int scale)
{
    for (int i = 0; s[i]; i++) {
        fb_draw_glyph_scaled(x + i * 6 * scale, y, font_get(s[i]), scale);
    }
}

/* ------------------------------------------------------------------ */
/* Public drawing API — thin wrappers for use by applets               */
/* ------------------------------------------------------------------ */
void ui_fb_clear(void)                              { fb_clear(); }
void ui_fb_pixel(int x, int y)                      { fb_set_pixel(x, y); }
void ui_fb_rect(int x, int y, int w, int h)         { fb_fill_rect(x, y, w, h); }
void ui_fb_hline(int y, int x0, int x1)
{
    for (int x = x0; x <= x1; x++) fb_set_pixel(x, y);
}
void ui_fb_text(int x, int y, const char *s)        { fb_draw_string(x, y, s, 0); }
void ui_fb_text_inv(int x, int y, const char *s)    { fb_draw_string(x, y, s, 1); }

/* ------------------------------------------------------------------ */
/* Widget renderers                                                     */
/* ------------------------------------------------------------------ */
static int g_signal  = 5;  /* 0–5  */
static int g_battery = 3;  /* 0–3  */

void ui_set_signal(int level)
{
    if (level < 0) level = 0;
    if (level > 5) level = 5;
    g_signal = level;
}

void ui_set_battery(int level)
{
    if (level < 0) level = 0;
    if (level > 3) level = 3;
    g_battery = level;
}

static void render_status_bar(void)
{
    /* Signal bars — five bars, bottom-aligned on the left side */
    for (int i = 0; i < 5; i++) {
        int h       = 2 + i * 2;
        int bar_bot = 11;
        if (i < g_signal) {
            for (int y = 0; y < h; y++) {
                fb_set_pixel(2 + i * 3, bar_bot - y);
            }
        } else {
            /* hollow: just the top and bottom pixel of each bar */
            fb_set_pixel(2 + i * 3, bar_bot);
            fb_set_pixel(2 + i * 3, bar_bot - h + 1);
        }
    }

    /* Battery icon — right side */
    {
        int bx = LCD_WIDTH - 13;
        int by = 2;
        /* outline: 10 wide × 7 tall */
        for (int i = 0; i < 10; i++) { fb_set_pixel(bx + i, by);     }
        for (int i = 0; i < 10; i++) { fb_set_pixel(bx + i, by + 6); }
        for (int i = 0; i <= 6;  i++) { fb_set_pixel(bx,     by + i); }
        for (int i = 0; i <= 6;  i++) { fb_set_pixel(bx + 9, by + i); }
        /* terminal nub */
        fb_set_pixel(bx + 10, by + 2);
        fb_set_pixel(bx + 10, by + 3);
        fb_set_pixel(bx + 10, by + 4);
        /* fill proportional to battery level */
        static const int fill_w[4] = {0, 2, 5, 8};
        int fw = fill_w[g_battery < 4 ? g_battery : 3];
        for (int fy = by + 1; fy <= by + 5; fy++) {
            for (int fx = bx + 1; fx < bx + 1 + fw; fx++) {
                fb_set_pixel(fx, fy);
            }
        }
    }

    /* Separator line below status bar */
    fb_hline(13);
}

static void render_softkey_bar(const char *left, const char *right)
{
    fb_hline(39);
    if (left  && *left)  fb_draw_string(1, 41, left, 0);
    if (right && *right) {
        int x = LCD_WIDTH - (int)strlen(right) * 6 - 1;
        if (x < 0) x = 0;
        fb_draw_string(x, 41, right, 0);
    }
}

static void render_menu_list(const widget_t *w, const screen_def_t *scr,
                              int cursor, int scroll)
{
    if (!scr->items || scr->item_count == 0) return;

    int visible = scr->item_count < MENU_VISIBLE_ITEMS
                  ? scr->item_count : MENU_VISIBLE_ITEMS;

    for (int i = 0; i < visible; i++) {
        int idx    = scroll + i;
        if (idx >= scr->item_count) break;
        int item_y = w->y + i * MENU_ITEM_H;

        if (idx == cursor) {
            /* Highlight: black background, white text via XOR */
            fb_fill_rect(0, item_y - 1, LCD_WIDTH, MENU_ITEM_H);
            fb_draw_string(w->x, item_y, scr->items[idx].label, 1);
        } else {
            fb_draw_string(w->x, item_y, scr->items[idx].label, 0);
        }
    }

    /* Scroll-up arrow */
    if (scroll > 0) {
        int ax = LCD_WIDTH - 5;
        int ay = w->y + 1;
        fb_set_pixel(ax + 1, ay);
        fb_set_pixel(ax,     ay + 2);
        fb_set_pixel(ax + 1, ay + 1);
        fb_set_pixel(ax + 2, ay + 2);
    }

    /* Scroll-down arrow */
    if (scroll + MENU_VISIBLE_ITEMS < scr->item_count) {
        int ax = LCD_WIDTH - 5;
        int ay = w->y + visible * MENU_ITEM_H - 4;
        fb_set_pixel(ax + 1, ay + 2);
        fb_set_pixel(ax,     ay);
        fb_set_pixel(ax + 1, ay + 1);
        fb_set_pixel(ax + 2, ay);
    }
}

/* ------------------------------------------------------------------ */
/* Navigation stack                                                     */
/* ------------------------------------------------------------------ */
typedef struct {
    int screen_id;
    int cursor;
    int scroll;
} nav_entry_t;

static const screen_def_t *g_screens;
static int                  g_screen_count;
static nav_entry_t          nav_stack[SCREEN_STACK_DEPTH];
static int                  nav_depth;

static nav_entry_t        *nav_top(void)
{
    if (nav_depth >= SCREEN_STACK_DEPTH) nav_depth = SCREEN_STACK_DEPTH - 1;
    return &nav_stack[nav_depth];
}
static const screen_def_t *cur_screen(void) { return &g_screens[nav_stack[nav_depth].screen_id]; }

static void exec_action(action_t a)
{
    switch (a.type) {
    case ACT_GOTO:
        if (a.screen_id >= 0 && a.screen_id < g_screen_count &&
            nav_depth < SCREEN_STACK_DEPTH - 1) {
            nav_depth++;
            nav_stack[nav_depth].screen_id = a.screen_id;
            nav_stack[nav_depth].cursor    = 0;
            nav_stack[nav_depth].scroll    = 0;
            /* Initialise applet when first entering an applet screen */
            {
                const screen_def_t *ns = &g_screens[a.screen_id];
                if (ns->type == SCREEN_APPLET &&
                        ns->applet && ns->applet->init) {
                    ns->applet->init();
                }
            }
        }
        break;
    case ACT_BACK:
        if (nav_depth > 0) nav_depth--;
        break;
    case ACT_CALL:
        if (a.fn) a.fn();
        break;
    case ACT_NONE:
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Key handling                                                         */
/* ------------------------------------------------------------------ */
void ui_inject_key(nokia_key_t key)
{
    const screen_def_t *scr = cur_screen();
    nav_entry_t        *nav = nav_top();

    /* Applet screens take all keys via their own handler */
    if (scr->type == SCREEN_APPLET) {
        if (scr->applet && scr->applet->on_key) {
            scr->applet->on_key(key);
        }
        return;
    }

    if (scr->type == SCREEN_MENU) {
        switch (key) {
        case KEY_UP:
            if (nav->cursor > 0) {
                nav->cursor--;
                if (nav->cursor < nav->scroll) nav->scroll = nav->cursor;
            }
            return;
        case KEY_DOWN:
            if (nav->cursor < scr->item_count - 1) {
                nav->cursor++;
                if (nav->cursor >= nav->scroll + MENU_VISIBLE_ITEMS) {
                    nav->scroll = nav->cursor - MENU_VISIBLE_ITEMS + 1;
                }
            }
            return;
        case KEY_OK:
        case KEY_LEFT:
            if (scr->items && nav->cursor < scr->item_count) {
                exec_action(scr->items[nav->cursor].on_select);
            }
            return;
        case KEY_RIGHT:
            exec_action(scr->on_right);
            return;
        default:
            break;
        }
    } else {
        switch (key) {
        case KEY_LEFT:  exec_action(scr->on_left);  return;
        case KEY_RIGHT: exec_action(scr->on_right); return;
        case KEY_UP:    exec_action(scr->on_up);    return;
        case KEY_DOWN:  exec_action(scr->on_down);  return;
        case KEY_OK:    exec_action(scr->on_ok);    return;
        default: break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Rendering                                                            */
/* ------------------------------------------------------------------ */
static void render_screen(void)
{
    const screen_def_t *scr = cur_screen();
    const nav_entry_t  *nav = nav_top();

    fb_clear();

    for (int i = 0; i < scr->widget_count; i++) {
        const widget_t *w = &scr->widgets[i];
        switch (w->type) {
        case WID_STATUS_BAR:
            render_status_bar();
            break;
        case WID_SOFTKEY_BAR:
            render_softkey_bar(w->text, w->text2);
            break;
        case WID_LABEL:
            if (w->scale > 1) {
                fb_draw_string_scaled(w->x, w->y, w->text, w->scale);
            } else {
                fb_draw_string(w->x, w->y, w->text, 0);
            }
            break;
        case WID_LABEL_CENTERED:
            fb_draw_string_centered(w->y, w->text);
            break;
        case WID_HLINE:
            fb_hline(w->y);
            break;
        case WID_MENU_LIST:
            render_menu_list(w, scr, nav->cursor, nav->scroll);
            break;
        default:
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */
void ui_init(const screen_def_t *screens, int count, int initial)
{
    g_screens              = screens;
    g_screen_count         = count;
    nav_depth              = 0;
    nav_stack[0].screen_id = initial;
    nav_stack[0].cursor    = 0;
    nav_stack[0].scroll    = 0;
}

void ui_tick(void)
{
    const screen_def_t *scr = cur_screen();

    if (scr->type == SCREEN_APPLET && scr->applet) {
        scr->applet->tick();
        scr->applet->render();
        return;
    }

    render_screen();
}

void ui_back(void)
{
    if (nav_depth > 0) nav_depth--;
}

int ui_current_screen_id(void)
{
    return nav_stack[nav_depth].screen_id;
}

int ui_current_menu_cursor(void)
{
    return nav_stack[nav_depth].cursor;
}

int ui_current_menu_scroll(void)
{
    return nav_stack[nav_depth].scroll;
}
