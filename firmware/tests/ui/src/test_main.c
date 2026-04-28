/*
 * Nokia 3310 UI — unit tests
 *
 * This file compiles in two modes:
 *
 *   STANDALONE (host, no Zephyr):
 *     Compile with plain gcc/cc.  See firmware/tests/Makefile.
 *     Run: make -C firmware/tests test
 *
 *   ZTEST (Zephyr native_sim):
 *     west build -b native_sim firmware/tests/ui
 *     west build -t run
 *     — or —
 *     west twister -T firmware/tests
 *
 * Test coverage:
 *   navigation   — init, GOTO, BACK, BACK-at-root, stack-overflow
 *   menu_nav     — cursor up/down clamping, scroll window, item selection
 *   call_action  — ACT_CALL fires callback
 *   validation   — ui_set_signal / ui_set_battery clamping
 *   rendering    — HLINE, SOFTKEY_BAR separator, LABEL, LABEL_CENTER,
 *                  STATUS_BAR signal + battery, menu selection highlight
 */

/* ------------------------------------------------------------------ */
/* Compatibility layer                                                  */
/* ------------------------------------------------------------------ */
#ifdef CONFIG_ZTEST
#  include <zephyr/ztest.h>
#  define TCHECK(c)      zassert_true((c), #c)
#  define TCHECK_EQ(a,b) zassert_equal((int)(a), (int)(b), #a " != " #b)
#else
#  include <stdio.h>
#  include <stdlib.h>
   static int _pass, _fail;
#  define TCHECK(c) \
       do { if (c) { _pass++; } else { \
           printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); _fail++; \
       } } while (0)
#  define TCHECK_EQ(a,b) \
       do { int _a = (int)(a), _b = (int)(b); \
           if (_a == _b) { _pass++; } else { \
               printf("FAIL %s:%d  %s = %d, want %d\n", \
                      __FILE__, __LINE__, #a, _a, _b); _fail++; \
           } } while (0)
#endif

#include "ui_framework.h"

/* ------------------------------------------------------------------ */
/* Test infrastructure                                                  */
/* ------------------------------------------------------------------ */

static int  call_count;
static void test_callback(void) { call_count++; }

/* pixel helpers */
static int pixel_at(int x, int y)
{
    if (x < 0 || x >= NOKIA_LCD_WIDTH || y < 0 || y >= NOKIA_LCD_HEIGHT)
        return 0;
    return (nokia_fb[y / 8][x] >> (y % 8)) & 1;
}

static int row_is_full(int y)
{
    for (int x = 0; x < NOKIA_LCD_WIDTH; x++)
        if (!pixel_at(x, y)) return 0;
    return 1;
}

static int row_is_clear(int y)
{
    for (int x = 0; x < NOKIA_LCD_WIDTH; x++)
        if (pixel_at(x, y)) return 0;
    return 1;
}

static int region_has_any_pixel(int x0, int y0, int w, int h)
{
    for (int x = x0; x < x0 + w; x++)
        for (int y = y0; y < y0 + h; y++)
            if (pixel_at(x, y)) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Test screen definitions                                              */
/* ------------------------------------------------------------------ */
enum {
    SCR_HOME = 0,
    SCR_MENU,
    SCR_DETAIL,
    SCR_STATUS,
    SCR_LOOP,
    T_SCR_COUNT,
};

static const menu_item_t menu_items[] = {
    { "Item 1", GOTO(SCR_DETAIL) },
    { "Item 2", GOTO(SCR_DETAIL) },
    { "Item 3", GOTO(SCR_DETAIL) },
    { "Item 4", GOTO(SCR_DETAIL) },
};

static const widget_t home_widgets[] = {
    HLINE(25),
    LABEL(0, 5, "Home"),
    SOFTKEY_BAR("GO", "B"),
};

static const widget_t menu_widgets[] = {
    MENU_LIST(),
    SOFTKEY_BAR("OK", "BACK"),
};

static const widget_t detail_widgets[] = {
    LABEL_CENTER(20, "Detail"),
    SOFTKEY_BAR("", "BACK"),
};

static const widget_t status_widgets[] = {
    STATUS_BAR(),
};

static const widget_t loop_widgets[] = {
    LABEL(0, 0, "X"),
};

static const screen_def_t t_screens[T_SCR_COUNT] = {
    [SCR_HOME] = {
        .name = "Home", .type = SCREEN_STATIC,
        .widgets = home_widgets, .widget_count = ARRAY_SIZE(home_widgets),
        .on_left  = GOTO(SCR_MENU),
        .on_right = BACK(),         /* BACK at root = no-op */
    },
    [SCR_MENU] = {
        .name = "Menu", .type = SCREEN_MENU,
        .widgets = menu_widgets, .widget_count = ARRAY_SIZE(menu_widgets),
        .items = menu_items, .item_count = ARRAY_SIZE(menu_items),
        .on_right = BACK(),
    },
    [SCR_DETAIL] = {
        .name = "Detail", .type = SCREEN_STATIC,
        .widgets = detail_widgets, .widget_count = ARRAY_SIZE(detail_widgets),
        .on_right = BACK(),
        .on_ok    = CALL(test_callback),
    },
    [SCR_STATUS] = {
        .name = "Status", .type = SCREEN_STATIC,
        .widgets = status_widgets, .widget_count = ARRAY_SIZE(status_widgets),
    },
    [SCR_LOOP] = {
        .name = "Loop", .type = SCREEN_STATIC,
        .widgets = loop_widgets, .widget_count = ARRAY_SIZE(loop_widgets),
        .on_left = GOTO(SCR_LOOP),  /* always pushes itself — overflow test */
    },
};

#define INIT()   ui_init(t_screens, T_SCR_COUNT, SCR_HOME)
#define INIT_AT(s) ui_init(t_screens, T_SCR_COUNT, (s))

/* ================================================================== */
/* SUITE: navigation                                                    */
/* ================================================================== */

static void test_nav_init(void)
{
    INIT();
    TCHECK_EQ(ui_current_screen_id(), SCR_HOME);
}

static void test_nav_goto(void)
{
    INIT();
    ui_inject_key(KEY_LEFT);            /* on_left = GOTO(SCR_MENU) */
    TCHECK_EQ(ui_current_screen_id(), SCR_MENU);
}

static void test_nav_back(void)
{
    INIT();
    ui_inject_key(KEY_LEFT);            /* HOME → MENU */
    TCHECK_EQ(ui_current_screen_id(), SCR_MENU);
    ui_inject_key(KEY_RIGHT);           /* MENU on_right = BACK() */
    TCHECK_EQ(ui_current_screen_id(), SCR_HOME);
}

static void test_nav_back_at_root_is_noop(void)
{
    INIT();
    ui_inject_key(KEY_RIGHT);           /* on_right = BACK() at depth 0 */
    TCHECK_EQ(ui_current_screen_id(), SCR_HOME);
}

static void test_nav_stack_overflow_clamped(void)
{
    INIT_AT(SCR_LOOP);
    /* Push SCR_LOOP onto itself more times than SCREEN_STACK_DEPTH (8) */
    for (int i = 0; i < 20; i++) {
        ui_inject_key(KEY_LEFT);
    }
    /* Should not crash, and current screen must still be valid */
    TCHECK_EQ(ui_current_screen_id(), SCR_LOOP);
}

/* ================================================================== */
/* SUITE: menu_nav                                                      */
/* ================================================================== */

static void test_menu_cursor_moves_down(void)
{
    INIT_AT(SCR_MENU);
    TCHECK_EQ(ui_current_menu_cursor(), 0);
    ui_inject_key(KEY_DOWN);
    TCHECK_EQ(ui_current_menu_cursor(), 1);
    ui_inject_key(KEY_DOWN);
    TCHECK_EQ(ui_current_menu_cursor(), 2);
}

static void test_menu_cursor_clamps_at_top(void)
{
    INIT_AT(SCR_MENU);
    TCHECK_EQ(ui_current_menu_cursor(), 0);
    ui_inject_key(KEY_UP);              /* already at top — must not go to -1 */
    TCHECK_EQ(ui_current_menu_cursor(), 0);
}

static void test_menu_cursor_clamps_at_bottom(void)
{
    INIT_AT(SCR_MENU);
    /* 4 items → max cursor = 3 */
    for (int i = 0; i < 10; i++) ui_inject_key(KEY_DOWN);
    TCHECK_EQ(ui_current_menu_cursor(), 3);
}

static void test_menu_scroll_follows_cursor(void)
{
    /* 4 items, 3 visible — scrolling kicks in at cursor = 3 */
    INIT_AT(SCR_MENU);
    TCHECK_EQ(ui_current_menu_scroll(), 0);
    ui_inject_key(KEY_DOWN);
    ui_inject_key(KEY_DOWN);
    ui_inject_key(KEY_DOWN);           /* cursor = 3 (beyond visible window) */
    TCHECK_EQ(ui_current_menu_cursor(), 3);
    TCHECK_EQ(ui_current_menu_scroll(), 1);
}

static void test_menu_scroll_up_follows_cursor(void)
{
    INIT_AT(SCR_MENU);
    for (int i = 0; i < 3; i++) ui_inject_key(KEY_DOWN); /* scroll = 1 */
    ui_inject_key(KEY_UP);                                 /* cursor = 2 */
    ui_inject_key(KEY_UP);                                 /* cursor = 1 */
    ui_inject_key(KEY_UP);                                 /* cursor = 0, scroll = 0 */
    TCHECK_EQ(ui_current_menu_cursor(), 0);
    TCHECK_EQ(ui_current_menu_scroll(), 0);
}

static void test_menu_ok_selects_item(void)
{
    INIT_AT(SCR_MENU);
    ui_inject_key(KEY_OK);              /* items[0] = GOTO(SCR_DETAIL) */
    TCHECK_EQ(ui_current_screen_id(), SCR_DETAIL);
}

static void test_menu_left_selects_item(void)
{
    /* In SCREEN_MENU, LEFT also triggers item selection */
    INIT_AT(SCR_MENU);
    ui_inject_key(KEY_LEFT);
    TCHECK_EQ(ui_current_screen_id(), SCR_DETAIL);
}

static void test_menu_right_is_back(void)
{
    INIT();
    ui_inject_key(KEY_LEFT);            /* HOME → MENU */
    ui_inject_key(KEY_RIGHT);           /* MENU on_right = BACK() */
    TCHECK_EQ(ui_current_screen_id(), SCR_HOME);
}

/* ================================================================== */
/* SUITE: call_action                                                   */
/* ================================================================== */

static void test_call_action_fires_callback(void)
{
    INIT_AT(SCR_DETAIL);
    call_count = 0;
    ui_inject_key(KEY_OK);              /* on_ok = CALL(test_callback) */
    TCHECK_EQ(call_count, 1);
}

static void test_call_action_fires_each_press(void)
{
    INIT_AT(SCR_DETAIL);
    call_count = 0;
    ui_inject_key(KEY_OK);
    ui_inject_key(KEY_OK);
    ui_inject_key(KEY_OK);
    TCHECK_EQ(call_count, 3);
}

/* ================================================================== */
/* SUITE: validation                                                    */
/* ================================================================== */

static void test_battery_clamps_negative(void)
{
    ui_set_battery(-5);
    INIT_AT(SCR_STATUS);
    ui_tick();
    /* Battery outline pixel at (72, 2) should always be set (top of outline) */
    /* Inner fill pixel at (72, 3) should NOT be set for level 0             */
    TCHECK(!pixel_at(72, 3));
}

static void test_battery_clamps_overflow(void)
{
    ui_set_battery(99);
    INIT_AT(SCR_STATUS);
    ui_tick();
    /* Level 3 (max): inner fill from x=72..79, y=3..7 — pixel (72,3) set */
    TCHECK(pixel_at(72, 3));
    ui_set_battery(3);                  /* restore to a valid value */
}

static void test_signal_clamps_negative(void)
{
    ui_set_signal(-1);
    INIT_AT(SCR_STATUS);
    ui_tick();
    /* All bars hollow: middle of tallest bar (x=14, y=5) must be clear */
    TCHECK(!pixel_at(14, 5));
    ui_set_signal(5);
}

static void test_signal_clamps_overflow(void)
{
    ui_set_signal(999);
    INIT_AT(SCR_STATUS);
    ui_tick();
    /* Clamped to 5 (full): tallest bar pixel (x=14, y=2) must be set */
    TCHECK(pixel_at(14, 2));
    ui_set_signal(5);
}

/* ================================================================== */
/* SUITE: rendering                                                     */
/* ================================================================== */

static void test_render_hline_sets_full_row(void)
{
    INIT();                             /* SCR_HOME has HLINE(25) */
    ui_tick();
    TCHECK(row_is_full(25));
    TCHECK(row_is_clear(24));
    TCHECK(row_is_clear(26));
}

static void test_render_softkey_separator(void)
{
    INIT();                             /* SCR_HOME has SOFTKEY_BAR */
    ui_tick();
    TCHECK(row_is_full(39));
}

static void test_render_label_sets_pixels(void)
{
    /* SCR_HOME has LABEL(0, 5, "Home") — 'H' col0=0x7F, row0 bit0=1 → (0,5) */
    INIT();
    ui_tick();
    TCHECK(pixel_at(0, 5));
}

static void test_render_label_center_sets_pixels(void)
{
    /* SCR_DETAIL has LABEL_CENTER(20, "Detail") — text centered in 84px */
    INIT_AT(SCR_DETAIL);
    ui_tick();
    TCHECK(region_has_any_pixel(0, 20, NOKIA_LCD_WIDTH, 7));
}

static void test_render_menu_selection_inverts_row(void)
{
    /* MENU_LIST() is at y=15; cursor=0 fill_rect(0, 14, 84, 9) */
    INIT_AT(SCR_MENU);
    ui_tick();
    TCHECK(row_is_full(14));            /* row above text — fully black */
}

static void test_render_signal_full(void)
{
    ui_set_signal(5);
    INIT_AT(SCR_STATUS);
    ui_tick();
    /* Tallest bar (i=4): filled from y=2 to y=11 — pixel (14,2) set */
    TCHECK(pixel_at(14, 2));
}

static void test_render_signal_zero(void)
{
    ui_set_signal(0);
    INIT_AT(SCR_STATUS);
    ui_tick();
    /* All bars hollow: middle pixel (14,5) must be clear */
    TCHECK(!pixel_at(14, 5));
    ui_set_signal(5);
}

static void test_render_battery_full(void)
{
    ui_set_battery(3);
    INIT_AT(SCR_STATUS);
    ui_tick();
    /* Inner fill at (72,3) for level 3 */
    TCHECK(pixel_at(72, 3));
}

static void test_render_battery_empty(void)
{
    ui_set_battery(0);
    INIT_AT(SCR_STATUS);
    ui_tick();
    /* No inner fill — (72,3) is clear */
    TCHECK(!pixel_at(72, 3));
    ui_set_battery(3);
}

/* ================================================================== */
/* Entry points                                                         */
/* ================================================================== */

#ifdef CONFIG_ZTEST

ZTEST_SUITE(navigation, NULL, NULL, NULL, NULL, NULL);
ZTEST(navigation, test_init)                { test_nav_init(); }
ZTEST(navigation, test_goto)                { test_nav_goto(); }
ZTEST(navigation, test_back)                { test_nav_back(); }
ZTEST(navigation, test_back_at_root_noop)   { test_nav_back_at_root_is_noop(); }
ZTEST(navigation, test_stack_overflow)      { test_nav_stack_overflow_clamped(); }

ZTEST_SUITE(menu_nav, NULL, NULL, NULL, NULL, NULL);
ZTEST(menu_nav, test_cursor_down)           { test_menu_cursor_moves_down(); }
ZTEST(menu_nav, test_cursor_up_clamp)       { test_menu_cursor_clamps_at_top(); }
ZTEST(menu_nav, test_cursor_down_clamp)     { test_menu_cursor_clamps_at_bottom(); }
ZTEST(menu_nav, test_scroll_follows_down)   { test_menu_scroll_follows_cursor(); }
ZTEST(menu_nav, test_scroll_follows_up)     { test_menu_scroll_up_follows_cursor(); }
ZTEST(menu_nav, test_ok_selects)            { test_menu_ok_selects_item(); }
ZTEST(menu_nav, test_left_selects)          { test_menu_left_selects_item(); }
ZTEST(menu_nav, test_right_is_back)         { test_menu_right_is_back(); }

ZTEST_SUITE(call_action, NULL, NULL, NULL, NULL, NULL);
ZTEST(call_action, test_fires_callback)     { test_call_action_fires_callback(); }
ZTEST(call_action, test_fires_each_press)   { test_call_action_fires_each_press(); }

ZTEST_SUITE(validation, NULL, NULL, NULL, NULL, NULL);
ZTEST(validation, test_battery_neg)         { test_battery_clamps_negative(); }
ZTEST(validation, test_battery_overflow)    { test_battery_clamps_overflow(); }
ZTEST(validation, test_signal_neg)          { test_signal_clamps_negative(); }
ZTEST(validation, test_signal_overflow)     { test_signal_clamps_overflow(); }

ZTEST_SUITE(rendering, NULL, NULL, NULL, NULL, NULL);
ZTEST(rendering, test_hline)                { test_render_hline_sets_full_row(); }
ZTEST(rendering, test_softkey_sep)          { test_render_softkey_separator(); }
ZTEST(rendering, test_label)                { test_render_label_sets_pixels(); }
ZTEST(rendering, test_label_center)         { test_render_label_center_sets_pixels(); }
ZTEST(rendering, test_menu_highlight)       { test_render_menu_selection_inverts_row(); }
ZTEST(rendering, test_signal_full)          { test_render_signal_full(); }
ZTEST(rendering, test_signal_zero)          { test_render_signal_zero(); }
ZTEST(rendering, test_battery_full)         { test_render_battery_full(); }
ZTEST(rendering, test_battery_empty)        { test_render_battery_empty(); }

#else  /* standalone */

int main(void)
{
    /* navigation */
    test_nav_init();
    test_nav_goto();
    test_nav_back();
    test_nav_back_at_root_is_noop();
    test_nav_stack_overflow_clamped();

    /* menu_nav */
    test_menu_cursor_moves_down();
    test_menu_cursor_clamps_at_top();
    test_menu_cursor_clamps_at_bottom();
    test_menu_scroll_follows_cursor();
    test_menu_scroll_up_follows_cursor();
    test_menu_ok_selects_item();
    test_menu_left_selects_item();
    test_menu_right_is_back();

    /* call_action */
    test_call_action_fires_callback();
    test_call_action_fires_each_press();

    /* validation */
    test_battery_clamps_negative();
    test_battery_clamps_overflow();
    test_signal_clamps_negative();
    test_signal_clamps_overflow();

    /* rendering */
    test_render_hline_sets_full_row();
    test_render_softkey_separator();
    test_render_label_sets_pixels();
    test_render_label_center_sets_pixels();
    test_render_menu_selection_inverts_row();
    test_render_signal_full();
    test_render_signal_zero();
    test_render_battery_full();
    test_render_battery_empty();

    int total = _pass + _fail;
    printf("\n%d/%d tests passed", _pass, total);
    if (_fail) printf("  (%d FAILED)", _fail);
    printf("\n");
    return _fail > 0 ? 1 : 0;
}

#endif /* CONFIG_ZTEST */
