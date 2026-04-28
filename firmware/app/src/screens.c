/*
 * Nokia 3310 UI — declarative screen definitions
 *
 * This is the only file you need to edit to define your UI.
 *
 * HOW TO ADD A SCREEN
 * -------------------
 * 1. Add an entry to the SCR_* enum below.
 * 2. Declare a widget array (what the screen looks like).
 * 3. For menus, declare a menu_item_t array (what items are listed).
 * 4. Add an entry to g_screens[] with the layout and button actions.
 *
 * SCREEN TYPES
 * ------------
 *   SCREEN_STATIC — a normal content screen.  Each button key maps to an
 *                   action via .on_left / .on_right / .on_up / .on_down /
 *                   .on_ok.
 *
 *   SCREEN_MENU   — a scrollable list.  UP/DOWN move the cursor; OK and
 *                   LEFT select the highlighted item.  RIGHT is routed to
 *                   .on_right (usually BACK()).
 *
 * ACTIONS
 * -------
 *   GOTO(id)   — push screen SCR_id onto the navigation stack.
 *   BACK()     — pop the navigation stack (return to previous screen).
 *   CALL(fn)   — call void fn(void).
 *   NO_ACT     — do nothing.
 *
 * WIDGETS
 * -------
 *   STATUS_BAR()               — signal bars + battery icon at the top.
 *   SOFTKEY_BAR("LEFT","RIGHT")— key labels at the bottom of the screen.
 *   HLINE(y)                   — full-width separator line at row y.
 *   LABEL(x, y, "text")        — left-aligned text.
 *   LABEL_SCALED(x, y, "t", s) — text drawn at scale s (2 = double-size).
 *   LABEL_CENTER(y, "text")    — centred text at row y.
 *   MENU_LIST()                — renders the item list for SCREEN_MENU.
 *
 * SCREEN LAYOUT (84 × 48 pixels)
 * --------------------------------
 *   y =  0–13 : status bar area (STATUS_BAR + separator)
 *   y = 14–38 : content / menu items
 *   y = 39–47 : softkey bar (separator + labels)
 */

#include <zephyr/kernel.h>
#include "ui_framework.h"

/* ------------------------------------------------------------------ */
/* Screen IDs                                                           */
/* ------------------------------------------------------------------ */
enum {
    SCR_IDLE = 0,
    SCR_MAIN_MENU,
    SCR_MESSAGES,
    SCR_GAMES,
    SCR_PROFILES,
    SCR_SETTINGS,
    SCR_COUNT,
};

/* ================================================================== */
/* Idle screen                                                          */
/* ================================================================== */
static const widget_t idle_widgets[] = {
    STATUS_BAR(),
    LABEL_SCALED(15, 18, "12:34", 2),
    LABEL_CENTER(30, "LORA"),
    SOFTKEY_BAR("MENU", "NAMES"),
};

/* ================================================================== */
/* Main menu                                                            */
/* ================================================================== */
static const menu_item_t main_menu_items[] = {
    { "Messages", GOTO(SCR_MESSAGES) },
    { "Games",    GOTO(SCR_GAMES)    },
    { "Profiles", GOTO(SCR_PROFILES) },
    { "Settings", GOTO(SCR_SETTINGS) },
};

static const widget_t main_menu_widgets[] = {
    LABEL_CENTER(1, "Menu"),
    HLINE(9),
    MENU_LIST(),
    SOFTKEY_BAR("SELECT", "BACK"),
};

/* ================================================================== */
/* Messages screen                                                      */
/* ================================================================== */
static const widget_t messages_widgets[] = {
    LABEL_CENTER(1, "Messages"),
    HLINE(9),
    LABEL_CENTER(22, "No messages"),
    SOFTKEY_BAR("", "BACK"),
};

/* ================================================================== */
/* Games screen                                                         */
/* ================================================================== */
static const widget_t games_widgets[] = {
    LABEL_CENTER(1, "Games"),
    HLINE(9),
    LABEL_CENTER(22, "Snake"),
    SOFTKEY_BAR("PLAY", "BACK"),
};

/* ================================================================== */
/* Profiles screen                                                      */
/* ================================================================== */
static const widget_t profiles_widgets[] = {
    LABEL_CENTER(1, "Profiles"),
    HLINE(9),
    LABEL_CENTER(18, "General"),
    LABEL_CENTER(27, "Silent"),
    SOFTKEY_BAR("SELECT", "BACK"),
};

/* ================================================================== */
/* Settings screen                                                      */
/* ================================================================== */
static const widget_t settings_widgets[] = {
    LABEL_CENTER(1, "Settings"),
    HLINE(9),
    LABEL_CENTER(22, "Coming soon"),
    SOFTKEY_BAR("", "BACK"),
};

/* ================================================================== */
/* Screen table — the complete UI definition                            */
/* Add, remove or reorder screens here; the engine adapts automatically */
/* ================================================================== */
const screen_def_t g_screens[SCR_COUNT] = {

    [SCR_IDLE] = {
        .name         = "Idle",
        .type         = SCREEN_STATIC,
        .widgets      = idle_widgets,
        .widget_count = ARRAY_SIZE(idle_widgets),
        .on_left      = GOTO(SCR_MAIN_MENU),
        /* on_right, on_up, on_down, on_ok default to NO_ACT (zero) */
    },

    [SCR_MAIN_MENU] = {
        .name         = "Main Menu",
        .type         = SCREEN_MENU,
        .widgets      = main_menu_widgets,
        .widget_count = ARRAY_SIZE(main_menu_widgets),
        .items        = main_menu_items,
        .item_count   = ARRAY_SIZE(main_menu_items),
        .on_right     = BACK(),
    },

    [SCR_MESSAGES] = {
        .name         = "Messages",
        .type         = SCREEN_STATIC,
        .widgets      = messages_widgets,
        .widget_count = ARRAY_SIZE(messages_widgets),
        .on_right     = BACK(),
    },

    [SCR_GAMES] = {
        .name         = "Games",
        .type         = SCREEN_STATIC,
        .widgets      = games_widgets,
        .widget_count = ARRAY_SIZE(games_widgets),
        .on_right     = BACK(),
    },

    [SCR_PROFILES] = {
        .name         = "Profiles",
        .type         = SCREEN_STATIC,
        .widgets      = profiles_widgets,
        .widget_count = ARRAY_SIZE(profiles_widgets),
        .on_right     = BACK(),
    },

    [SCR_SETTINGS] = {
        .name         = "Settings",
        .type         = SCREEN_STATIC,
        .widgets      = settings_widgets,
        .widget_count = ARRAY_SIZE(settings_widgets),
        .on_right     = BACK(),
    },
};

const int g_screen_count = SCR_COUNT;
