/*
 * Nokia 3310 declarative UI framework
 *
 * Screens are defined as static const data structures.  The engine handles
 * rendering and navigation so the application only needs to describe *what*
 * should appear and *what* should happen when a key is pressed.
 *
 * LCD: 84 × 48 pixels (Nokia PCD8544 / 5110 format)
 * Keys: LEFT (left softkey), RIGHT (right softkey), UP, DOWN, OK
 *
 * Quick-start example (screens.c):
 *
 *   enum { SCR_IDLE = 0, SCR_MENU, SCR_COUNT };
 *
 *   static const menu_item_t menu_items[] = {
 *       { "Messages", GOTO(SCR_IDLE) },
 *   };
 *
 *   static const widget_t idle_widgets[] = {
 *       STATUS_BAR(),
 *       LABEL_CENTER(20, "Nokia"),
 *       SOFTKEY_BAR("MENU", ""),
 *   };
 *
 *   const screen_def_t g_screens[SCR_COUNT] = {
 *       [SCR_IDLE] = {
 *           .name = "Idle", .type = SCREEN_STATIC,
 *           .widgets = idle_widgets, .widget_count = ARRAY_SIZE(idle_widgets),
 *           .on_left = GOTO(SCR_MENU),
 *       },
 *       [SCR_MENU] = {
 *           .name = "Menu", .type = SCREEN_MENU,
 *           .widgets = menu_widgets, .widget_count = ARRAY_SIZE(menu_widgets),
 *           .items = menu_items,    .item_count  = ARRAY_SIZE(menu_items),
 *           .on_right = BACK(),
 *       },
 *   };
 *   const int g_screen_count = SCR_COUNT;
 *
 * In main():
 *   ui_init(g_screens, g_screen_count, SCR_IDLE);
 *   // then call ui_tick() each loop iteration
 */

#pragma once
#include <stdint.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

/* ------------------------------------------------------------------ */
/* LCD geometry — also used by tests to inspect nokia_fb               */
/* ------------------------------------------------------------------ */
#define NOKIA_LCD_WIDTH   84
#define NOKIA_LCD_HEIGHT  48
#define NOKIA_LCD_PAGES   (NOKIA_LCD_HEIGHT / 8)

/*
 * Framebuffer: 6 pages × 84 bytes, 1 bit per pixel (PCD8544 format).
 * This symbol is placed at a fixed address so Renode / lcd_viewer.py
 * can read it directly from RAM.  Tests inspect it after ui_tick().
 */
extern uint8_t nokia_fb[NOKIA_LCD_PAGES][NOKIA_LCD_WIDTH];

/* ------------------------------------------------------------------ */
/* Keys                                                                 */
/* ------------------------------------------------------------------ */
typedef enum {
    KEY_NONE  = 0x00,
    KEY_LEFT  = 0x01,   /* left soft key  */
    KEY_RIGHT = 0x02,   /* right soft key */
    KEY_UP    = 0x04,
    KEY_DOWN  = 0x08,
    KEY_OK    = 0x10,
} nokia_key_t;

/* ------------------------------------------------------------------ */
/* Applet interface                                                     */
/*                                                                      */
/* An applet is a self-contained interactive mode (e.g. a game) that   */
/* takes over rendering and key handling while it is on the nav stack.  */
/* Register one by using SCREEN_APPLET in a screen_def_t.               */
/* ------------------------------------------------------------------ */
typedef struct {
    /** Called once when the applet screen is pushed onto the nav stack. */
    void (*init)(void);
    /** Called every ui_tick().  Update game state here (do not draw). */
    void (*tick)(void);
    /** Called every ui_tick() after tick().  Draw to nokia_fb here.   */
    void (*render)(void);
    /** Called when any key is pressed.  Call ui_back() to exit.       */
    void (*on_key)(nokia_key_t key);
} applet_t;

/* ------------------------------------------------------------------ */
/* Actions                                                              */
/* ------------------------------------------------------------------ */
typedef enum {
    ACT_NONE,
    ACT_GOTO,   /* push a screen onto the navigation stack */
    ACT_BACK,   /* pop the navigation stack                */
    ACT_CALL,   /* call a void function                    */
} action_type_t;

typedef struct {
    action_type_t  type;
    int            screen_id;   /* target for ACT_GOTO          */
    void         (*fn)(void);   /* callback for ACT_CALL        */
} action_t;

/*
 * Action initializer macros.  Use these only as struct/array initializers,
 * not in assignment expressions (C99 limitation for static storage).
 */
#define GOTO(id)   { ACT_GOTO, (id), 0 }
#define BACK()     { ACT_BACK, 0,    0 }
#define CALL(fn_)  { ACT_CALL, 0,    (fn_) }
#define NO_ACT     { ACT_NONE, 0,    0 }

/* ------------------------------------------------------------------ */
/* Widgets                                                              */
/* ------------------------------------------------------------------ */
typedef enum {
    WID_LABEL,          /* text at (x, y); scale = font multiplier    */
    WID_LABEL_CENTERED, /* text centered horizontally at row y         */
    WID_STATUS_BAR,     /* signal bars + battery icon (top of screen)  */
    WID_SOFTKEY_BAR,    /* left/right key labels (bottom of screen)    */
    WID_HLINE,          /* full-width horizontal line at row y         */
    WID_MENU_LIST,      /* scrollable item list (SCREEN_MENU only)     */
} widget_type_t;

typedef struct {
    widget_type_t  type;
    int            x, y;
    int            scale;   /* font scale factor (1 = normal, 2 = double, …) */
    const char    *text;    /* label text, or left softkey label              */
    const char    *text2;   /* right softkey label (WID_SOFTKEY_BAR only)     */
} widget_t;

/* Widget initializer macros */
#define LABEL(px, py, str)            { WID_LABEL,          (px), (py), 1,    (str), 0    }
#define LABEL_SCALED(px, py, str, sc) { WID_LABEL,          (px), (py), (sc), (str), 0    }
#define LABEL_CENTER(py, str)         { WID_LABEL_CENTERED, 0,    (py), 1,    (str), 0    }
#define STATUS_BAR()                  { WID_STATUS_BAR,     0,    0,    1,    0,     0    }
#define SOFTKEY_BAR(l, r)             { WID_SOFTKEY_BAR,    0,    40,   1,    (l),   (r)  }
#define HLINE(py)                     { WID_HLINE,          0,    (py), 1,    0,     0    }
#define MENU_LIST()                   { WID_MENU_LIST,      2,    15,   1,    0,     0    }

/* ------------------------------------------------------------------ */
/* Menu items (used by SCREEN_MENU)                                     */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *label;
    action_t    on_select;  /* action to execute when this item is chosen */
} menu_item_t;

/* ------------------------------------------------------------------ */
/* Screen descriptor                                                    */
/* ------------------------------------------------------------------ */
typedef enum {
    SCREEN_STATIC,   /* static content; all keys routed via on_* fields  */
    SCREEN_MENU,     /* scrollable list; UP/DOWN/OK handled automatically */
    SCREEN_APPLET,   /* game / interactive mode; delegates to applet_t    */
} screen_type_t;

#define SCREEN_STACK_DEPTH 8

typedef struct {
    const char          *name;
    screen_type_t        type;

    const widget_t      *widgets;
    int                  widget_count;

    /* SCREEN_MENU only — ignored for SCREEN_STATIC */
    const menu_item_t   *items;
    int                  item_count;

    /*
     * Button actions.
     * SCREEN_STATIC : all five keys are routed here.
     * SCREEN_MENU   : UP/DOWN/OK select items automatically;
     *                 LEFT executes the highlighted item (= OK);
     *                 RIGHT is routed to on_right (usually BACK()).
     * SCREEN_APPLET : ignored — all keys go to applet->on_key().
     */
    action_t             on_left;
    action_t             on_right;
    action_t             on_up;
    action_t             on_down;
    action_t             on_ok;

    /* SCREEN_APPLET only — pointer to the applet implementation */
    const applet_t      *applet;
} screen_def_t;

/* Convenience macro for declaring an applet screen in g_screens[].    */
/* All unspecified fields (widgets, items, actions) are zero-inited.   */
#define APPLET_SCREEN(n, app_ptr) \
    { .name = (n), .type = SCREEN_APPLET, .applet = (app_ptr) }

/* ------------------------------------------------------------------ */
/* Engine API                                                           */
/* ------------------------------------------------------------------ */

/**
 * Initialise the UI engine.
 * @param screens  Array of screen descriptors (must remain valid forever).
 * @param count    Number of entries in @p screens.
 * @param initial  Index of the first screen to display.
 */
void ui_init(const screen_def_t *screens, int count, int initial);

/** Render the current screen into the framebuffer.  Call once per tick. */
void ui_tick(void);

/** Feed a key event into the engine (called from main loop). */
void ui_inject_key(nokia_key_t key);

/** Update the signal level shown in the status bar (0 = none, 5 = full). */
void ui_set_signal(int level);

/** Update the battery level shown in the status bar (0 = empty, 3 = full). */
void ui_set_battery(int level);

/* ------------------------------------------------------------------ */
/* Inspection helpers — primarily for unit tests                        */
/* ------------------------------------------------------------------ */

/** Return the screen ID currently at the top of the navigation stack. */
int ui_current_screen_id(void);

/** Return the menu cursor position in the current SCREEN_MENU. */
int ui_current_menu_cursor(void);

/** Return the menu scroll offset in the current SCREEN_MENU. */
int ui_current_menu_scroll(void);

/* ------------------------------------------------------------------ */
/* Navigation helper — for use by applets                              */
/* ------------------------------------------------------------------ */

/** Pop the current screen off the nav stack (return to previous screen). */
void ui_back(void);

/* ------------------------------------------------------------------ */
/* Drawing primitives — for use by applets                             */
/* ------------------------------------------------------------------ */

/** Clear the entire framebuffer (all pixels off). */
void ui_fb_clear(void);

/** Set a single pixel at (x, y). Coordinates are clipped silently. */
void ui_fb_pixel(int x, int y);

/** Fill a w×h rectangle with its top-left corner at (x, y). */
void ui_fb_rect(int x, int y, int w, int h);

/** Draw a horizontal line from x0 to x1 (inclusive) at row y. */
void ui_fb_hline(int y, int x0, int x1);

/** Draw a null-terminated string with its top-left at (x, y). */
void ui_fb_text(int x, int y, const char *s);

/** Same as ui_fb_text but pixels are XOR-flipped (white-on-black). */
void ui_fb_text_inv(int x, int y, const char *s);
