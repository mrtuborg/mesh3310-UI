/*
 * Nokia 3310 Snake
 *
 * Architecture: implements the applet_t interface so the UI framework
 * can treat it like any other screen on the navigation stack.  The
 * framework calls init() once on entry, tick()+render() every 100 ms,
 * and on_key() for every keypress.  Calling ui_back() from on_key()
 * pops Snake off the stack and returns to the previous screen.
 *
 * Grid: 21 × 10 cells at CELL=4 px each.
 * Layout:
 *   y = 0–6   score header ("SNAKE" left, score right)
 *   y = 7     divider
 *   y = 8–47  game area (10 rows × 21 columns)
 *
 * Controls (during play):
 *   UP / DOWN / LEFT / RIGHT  — steer
 *   OK                        — pause / unpause
 * Controls (paused or game-over):
 *   OK                        — resume / restart
 *   RIGHT                     — exit to menu
 */

#include "ui_framework.h"
#include "game_snake.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Grid geometry                                                        */
/* ------------------------------------------------------------------ */
#define CELL          4                              /* pixels per cell */
#define GW            (NOKIA_LCD_WIDTH  / CELL)      /* 21 columns      */
#define GH            ((NOKIA_LCD_HEIGHT - 8) / CELL)/* 10 rows         */
#define GAME_Y        8                              /* top of game area */
#define SNAKE_MAX     55                             /* max snake length */
#define INITIAL_SPEED 4                              /* ticks per move   */

/* ------------------------------------------------------------------ */
/* State                                                                */
/* ------------------------------------------------------------------ */
typedef struct { int8_t x, y; } pt_t;

static pt_t     g_body[SNAKE_MAX];  /* [0] = tail,  [len-1] = head */
static int      g_len;
static int8_t   g_dx, g_dy;        /* current direction            */
static pt_t     g_food;
static int      g_score;
static bool     g_game_over;
static bool     g_paused;
static int      g_ticks;           /* frame counter                */
static int      g_speed;           /* ticks between snake moves    */
static uint8_t  g_game_count;      /* seeds RNG differently each game */
static uint16_t g_rng;

/* ------------------------------------------------------------------ */
/* RNG — xorshift16                                                     */
/* ------------------------------------------------------------------ */
static uint16_t rng_next(void)
{
    g_rng ^= (uint16_t)(g_rng << 7);
    g_rng ^= (uint16_t)(g_rng >> 9);
    g_rng ^= (uint16_t)(g_rng << 8);
    return g_rng;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */
static bool is_snake(int x, int y)
{
    for (int i = 0; i < g_len; i++) {
        if (g_body[i].x == x && g_body[i].y == y) return true;
    }
    return false;
}

static void place_food(void)
{
    pt_t f;
    int  tries = 0;
    do {
        f.x = (int8_t)(rng_next() % GW);
        f.y = (int8_t)(rng_next() % GH);
        tries++;
    } while (is_snake(f.x, f.y) && tries < GW * GH);
    g_food = f;
}

/* ------------------------------------------------------------------ */
/* Reset — also used as the applet init()                               */
/* ------------------------------------------------------------------ */
static void snake_reset(void)
{
    /* Different seed each game */
    g_rng = (uint16_t)(1 + g_game_count++);

    g_len = 3;
    g_dx  = 1;
    g_dy  = 0;

    /* Start horizontally in the middle-left of the grid */
    for (int i = 0; i < g_len; i++) {
        g_body[i].x = (int8_t)i;
        g_body[i].y = (int8_t)(GH / 2);
    }

    g_score     = 0;
    g_speed     = INITIAL_SPEED;
    g_ticks     = 0;
    g_game_over = false;
    g_paused    = false;

    place_food();
}

/* ------------------------------------------------------------------ */
/* Move snake one cell in the current direction                         */
/* ------------------------------------------------------------------ */
static void snake_move(void)
{
    pt_t head = {
        (int8_t)(g_body[g_len - 1].x + g_dx),
        (int8_t)(g_body[g_len - 1].y + g_dy)
    };

    /* Wall collision */
    if (head.x < 0 || head.x >= GW || head.y < 0 || head.y >= GH) {
        g_game_over = true;
        return;
    }

    /* Self collision — skip tail (index 0) which moves away this tick */
    for (int i = 1; i < g_len; i++) {
        if (g_body[i].x == head.x && g_body[i].y == head.y) {
            g_game_over = true;
            return;
        }
    }

    bool ate = (head.x == g_food.x && head.y == g_food.y);

    if (!ate) {
        /* Drop tail: shift body left by one */
        memmove(g_body, g_body + 1, (size_t)(g_len - 1) * sizeof(pt_t));
        g_len--;
    }

    /* Append new head */
    if (g_len < SNAKE_MAX) {
        g_body[g_len++] = head;
    }

    if (ate) {
        g_score++;
        /* Speed up every 3 food items, cap at 1 tick/move (10 Hz) */
        g_speed = INITIAL_SPEED - g_score / 3;
        if (g_speed < 1) g_speed = 1;
        place_food();
    }
}

/* ------------------------------------------------------------------ */
/* Applet tick — update state only, no drawing                          */
/* ------------------------------------------------------------------ */
static void snake_tick(void)
{
    if (g_game_over || g_paused) return;
    if (++g_ticks % g_speed == 0) {
        snake_move();
    }
}

/* ------------------------------------------------------------------ */
/* Applet render — write to nokia_fb                                    */
/* ------------------------------------------------------------------ */
static void snake_render(void)
{
    ui_fb_clear();

    if (g_game_over) {
        char buf[16];
        snprintf(buf, sizeof(buf), "SCORE: %d", g_score);
        ui_fb_text_inv(15, 3,  "  GAME OVER  ");
        ui_fb_text(17,     15, buf);
        ui_fb_text(1,      28, "OK:replay");
        ui_fb_text(1,      37, "RIGHT:menu");
        return;
    }

    /* Score header */
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", g_score);
        ui_fb_text(0, 0, "SNAKE");
        int sx = NOKIA_LCD_WIDTH - (int)strlen(buf) * 6;
        if (sx > 0) ui_fb_text(sx, 0, buf);
    }

    /* Divider */
    ui_fb_hline(GAME_Y - 1, 0, NOKIA_LCD_WIDTH - 1);

    /* Food — 2×2 dot centred in its cell */
    ui_fb_rect(g_food.x * CELL + 1,
               GAME_Y + g_food.y * CELL + 1,
               2, 2);

    /* Snake body — 3×3 blocks with 1 px gap; head is a solid 4×4 */
    for (int i = 0; i < g_len - 1; i++) {
        ui_fb_rect(g_body[i].x * CELL,
                   GAME_Y + g_body[i].y * CELL,
                   CELL - 1, CELL - 1);
    }
    if (g_len > 0) {
        int i = g_len - 1;
        ui_fb_rect(g_body[i].x * CELL,
                   GAME_Y + g_body[i].y * CELL,
                   CELL, CELL);   /* head: full cell, visually distinct */
    }

    /* Pause overlay */
    if (g_paused) {
        ui_fb_text_inv(25, 19, " PAUSE ");
        ui_fb_text(1, 28, "OK:resume");
        ui_fb_text(1, 37, "RIGHT:menu");
    }
}

/* ------------------------------------------------------------------ */
/* Applet key handler                                                   */
/* ------------------------------------------------------------------ */
static void snake_on_key(nokia_key_t key)
{
    if (g_game_over) {
        if (key == KEY_OK)    { snake_reset(); return; }
        if (key == KEY_RIGHT) { ui_back();     return; }
        return;
    }

    if (g_paused) {
        if (key == KEY_OK)    { g_paused = false; return; }
        if (key == KEY_RIGHT) { ui_back();         return; }
        return;
    }

    switch (key) {
    /* Steer — 180° reversals are blocked */
    case KEY_UP:    if (g_dy == 0) { g_dx =  0; g_dy = -1; } break;
    case KEY_DOWN:  if (g_dy == 0) { g_dx =  0; g_dy =  1; } break;
    case KEY_LEFT:  if (g_dx == 0) { g_dx = -1; g_dy =  0; } break;
    case KEY_RIGHT: if (g_dx == 0) { g_dx =  1; g_dy =  0; } break;
    case KEY_OK:    g_paused = true; break;
    default: break;
    }
}

/* ------------------------------------------------------------------ */
/* Exported applet descriptor                                           */
/* ------------------------------------------------------------------ */
const applet_t snake_applet = {
    .init   = snake_reset,
    .tick   = snake_tick,
    .render = snake_render,
    .on_key = snake_on_key,
};
