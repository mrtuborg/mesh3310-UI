/*
 * Nokia 3310 Messages Applet  -  T9 multi-tap text entry
 *
 * States
 * ------
 *   MS_INBOX    -  scrollable inbox with 3 pre-filled messages
 *   MS_VIEW     -  full-screen message reader
 *   MS_COMPOSE  -  text compose with T9 multi-tap input
 *   MS_SENT     -  "Message sent!" confirmation (auto-dismisses)
 *
 * T9 key map (multi-tap  -  same key cycles through characters)
 * -----------------------------------------------------------
 *   0: " 0"    1: ".,!?"   2: "abc"   3: "def"
 *   4: "ghi"   5: "jkl"    6: "mno"   7: "pqrs"
 *   8: "tuv"   9: "wxyz"
 *   *   -  toggle uppercase / lowercase (commits pending char first)
 *   #   -  insert space (commits pending char first)
 *
 * Key bindings
 * ------------
 *   Inbox:   UP/DOWN scroll, OK = view, LEFT SK = New, RIGHT SK = Back
 *   View:    any key = back to inbox
 *   Compose: digit/star/hash keys via nokia_char_raw (T9 input)
 *            LEFT SK  = backspace / cancel (empty -> back to inbox)
 *            RIGHT SK = Send
 *            OK       = commit pending T9 char
 *   Sent:    any key = back to inbox (also auto-dismisses after 1.5 s)
 */

#include "ui_framework.h"
#include "applet_messages.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* Character key input written by lcd_viewer.py */
extern volatile uint8_t nokia_char_raw;

/* ------------------------------------------------------------------ */
/* Constants                                                            */
/* ------------------------------------------------------------------ */
#define MSG_MAX_LEN      64        /* max chars in compose buffer      */
#define CHARS_PER_LINE   14        /* 6 px/char x 14 = 84 px           */
#define T9_TIMEOUT       8         /* ticks before pending char commits */
#define INBOX_COUNT      3
#define SENT_HOLD_TICKS  15        /* auto-dismiss "Sent!" after 1.5 s  */

/* Vertical layout (pixels) */
#define TITLE_Y          1
#define HLINE_Y          9
#define CONTENT_Y        12        /* first content row                 */
#define CONTENT_ROW_H    9         /* pixels per text row               */
#define SK_HLINE_Y       39
#define SK_TEXT_Y        41

/* ------------------------------------------------------------------ */
/* T9 character map                                                     */
/* ------------------------------------------------------------------ */
static const char * const T9_CHARS[10] = {
    " 0",    /* 0 */
    ".,!?",  /* 1 */
    "abc",   /* 2 */
    "def",   /* 3 */
    "ghi",   /* 4 */
    "jkl",   /* 5 */
    "mno",   /* 6 */
    "pqrs",  /* 7 */
    "tuv",   /* 8 */
    "wxyz",  /* 9 */
};

/* ------------------------------------------------------------------ */
/* Pre-filled inbox messages                                            */
/* ------------------------------------------------------------------ */
static const char * const INBOX_FROM[INBOX_COUNT] = {
    "Alice", "Bob", "Mom"
};

static const char * const INBOX_TEXT[INBOX_COUNT] = {
    "Coming tonight?",
    "Check the news",
    "Call me pls",
};

/* ------------------------------------------------------------------ */
/* State                                                                */
/* ------------------------------------------------------------------ */
typedef enum { MS_INBOX, MS_VIEW, MS_COMPOSE, MS_SENT } msg_state_t;

static msg_state_t state;
static int8_t      inbox_cursor;   /* 0 .. INBOX_COUNT-1               */
static int8_t      view_idx;       /* message currently being read      */

/* Compose text buffer */
static char    compose_buf[MSG_MAX_LEN + 1];
static int     compose_len;

/* T9 input state */
static int8_t  t9_key;     /* current key index (0-9), -1 = none       */
static uint8_t t9_tap;     /* how many times the key has been pressed   */
static uint8_t t9_timer;   /* ticks elapsed since last tap              */
static bool    t9_upper;   /* uppercase mode flag                       */

/* UI helpers */
static uint8_t blink_ticks;   /* drives cursor blink (bit 2 used)       */
static uint8_t sent_ticks;    /* countdown for MS_SENT auto-dismiss      */

/* ------------------------------------------------------------------ */
/* T9 helpers                                                           */
/* ------------------------------------------------------------------ */
static char t9_pending_char(void)
{
    if (t9_key < 0) return '\0';
    const char *s = T9_CHARS[(uint8_t)t9_key];
    int   len = (int)strlen(s);
    char  c   = s[t9_tap % (unsigned)len];
    if (t9_upper && c >= 'a' && c <= 'z') c = (char)(c - 32);
    return c;
}

static void t9_commit(void)
{
    if (t9_key < 0) return;
    char c = t9_pending_char();
    if (c && compose_len < MSG_MAX_LEN) {
        compose_buf[compose_len++] = c;
        compose_buf[compose_len]   = '\0';
    }
    t9_key   = -1;
    t9_tap   = 0;
    t9_timer = 0;
}

static void t9_handle(uint8_t ascii_ch)
{
    if (ascii_ch == '*') {          /* toggle case; commit first         */
        t9_commit();
        t9_upper = !t9_upper;
        return;
    }
    if (ascii_ch == '#') {          /* insert space; commit first        */
        t9_commit();
        if (compose_len < MSG_MAX_LEN) {
            compose_buf[compose_len++] = ' ';
            compose_buf[compose_len]   = '\0';
        }
        return;
    }
    if (ascii_ch < '0' || ascii_ch > '9') return;

    int key = ascii_ch - '0';
    if (t9_key == (int8_t)key) {    /* same key: advance cycle           */
        t9_tap++;
        t9_timer = 0;
    } else {                        /* different key: commit + start new */
        t9_commit();
        t9_key   = (int8_t)key;
        t9_tap   = 0;
        t9_timer = 0;
    }
}

static void compose_reset(void)
{
    compose_len    = 0;
    compose_buf[0] = '\0';
    t9_key         = -1;
    t9_tap         = 0;
    t9_timer       = 0;
    t9_upper       = true;  /* Nokia default: start uppercase */
}

/* ------------------------------------------------------------------ */
/* Drawing helpers                                                      */
/* ------------------------------------------------------------------ */
static void draw_title(const char *title)
{
    int len = (int)strlen(title);
    int x   = (NOKIA_LCD_WIDTH - len * 6) / 2;
    if (x < 0) x = 0;
    ui_fb_text(x, TITLE_Y, title);
    ui_fb_hline(HLINE_Y, 0, NOKIA_LCD_WIDTH - 1);
}

static void draw_softkeys(const char *left_sk, const char *right_sk)
{
    ui_fb_hline(SK_HLINE_Y, 0, NOKIA_LCD_WIDTH - 1);
    if (left_sk  && *left_sk)  ui_fb_text(1, SK_TEXT_Y, left_sk);
    if (right_sk && *right_sk) {
        int rx = NOKIA_LCD_WIDTH - (int)strlen(right_sk) * 6 - 1;
        if (rx < 0) rx = 0;
        ui_fb_text(rx, SK_TEXT_Y, right_sk);
    }
}

/* Render one inbox row; selected rows are inverted */
static void draw_inbox_row(int idx, int y, bool selected)
{
    char line[CHARS_PER_LINE + 1];
    snprintf(line, sizeof(line), "%s: %s", INBOX_FROM[idx], INBOX_TEXT[idx]);

    if (selected) {
        /* Fill a highlight bar, then draw text inverted (XOR -> white text) */
        ui_fb_rect(0, y - 1, NOKIA_LCD_WIDTH, CONTENT_ROW_H);
        ui_fb_text_inv(0, y, line);
    } else {
        ui_fb_text(2, y, line);
    }
}

/* ------------------------------------------------------------------ */
/* Applet callbacks                                                     */
/* ------------------------------------------------------------------ */
static void msg_init(void)
{
    state        = MS_INBOX;
    inbox_cursor = 0;
    view_idx     = 0;
    blink_ticks  = 0;
    sent_ticks   = 0;
    compose_reset();
    nokia_char_raw = 0;  /* discard any stale key press */
}

static void msg_tick(void)
{
    blink_ticks++;

    /* Consume character key from lcd_viewer (T9 input) */
    uint8_t ch = nokia_char_raw;
    if (ch) {
        nokia_char_raw = 0;
        if (state == MS_COMPOSE) {
            t9_handle(ch);
        }
    }

    /* T9 timeout: auto-commit if no tap for T9_TIMEOUT ticks */
    if (state == MS_COMPOSE && t9_key >= 0) {
        if (++t9_timer >= T9_TIMEOUT) {
            t9_commit();
        }
    }

    /* Auto-dismiss the "Sent!" screen */
    if (state == MS_SENT && ++sent_ticks >= SENT_HOLD_TICKS) {
        state = MS_INBOX;
    }
}

static void msg_render(void)
{
    ui_fb_clear();

    switch (state) {

    /* ---------------------------------------------------------------- */
    case MS_INBOX:
        draw_title("Messages");
        for (int i = 0; i < INBOX_COUNT; i++) {
            draw_inbox_row(i, CONTENT_Y + i * CONTENT_ROW_H,
                           i == inbox_cursor);
        }
        draw_softkeys("NEW", "BACK");
        break;

    /* ---------------------------------------------------------------- */
    case MS_VIEW: {
        draw_title(INBOX_FROM[view_idx]);
        /* Render message text, wrapping at CHARS_PER_LINE */
        const char *text = INBOX_TEXT[view_idx];
        int         pos  = 0;
        int         line = 0;
        int         tlen = (int)strlen(text);
        while (pos < tlen && line < 3) {
            char   row[CHARS_PER_LINE + 1];
            int    n = tlen - pos;
            if (n > CHARS_PER_LINE) n = CHARS_PER_LINE;
            memcpy(row, text + pos, n);
            row[n] = '\0';
            ui_fb_text(0, CONTENT_Y + line * CONTENT_ROW_H, row);
            pos  += n;
            line++;
        }
        draw_softkeys("", "BACK");
        break;
    }

    /* ---------------------------------------------------------------- */
    case MS_COMPOSE: {
        /* Title shows current case mode */
        draw_title(t9_upper ? "Write Msg [A]" : "Write Msg [a]");

        /*
         * Display the last 3 lines of the compose buffer.
         * The cursor always sits on cursor_line; scroll so it stays visible.
         */
        int cursor_line = compose_len / CHARS_PER_LINE;
        int start_line  = (cursor_line >= 3) ? (cursor_line - 2) : 0;

        for (int row = 0; row < 3; row++) {
            int src  = start_line + row;
            int y    = CONTENT_Y + row * CONTENT_ROW_H;
            int coff = src * CHARS_PER_LINE;  /* char offset for this line */

            if (coff > compose_len) break;

            /* Characters already committed on this line */
            int n = compose_len - coff;
            if (n > CHARS_PER_LINE) n = CHARS_PER_LINE;

            if (n > 0) {
                char seg[CHARS_PER_LINE + 1];
                memcpy(seg, compose_buf + coff, n);
                seg[n] = '\0';
                ui_fb_text(0, y, seg);
            }

            /* Cursor / pending T9 char  -  only on the active line */
            if (src == cursor_line) {
                int cx = n * 6;
                if (cx > NOKIA_LCD_WIDTH - 6) cx = NOKIA_LCD_WIDTH - 6;

                if (t9_key >= 0) {
                    /* Pending char: fill cell, then draw inverted */
                    ui_fb_rect(cx, y - 1, 6, CONTENT_ROW_H);
                    char pending[2] = { t9_pending_char(), '\0' };
                    ui_fb_text_inv(cx, y, pending);
                } else {
                    /* Blinking underscore cursor (toggles every 4 ticks) */
                    if ((blink_ticks >> 2) & 1) {
                        ui_fb_hline(y + 7, cx, cx + 4);
                    }
                }
            }
        }

        draw_softkeys("DEL", "SEND");
        break;
    }

    /* ---------------------------------------------------------------- */
    case MS_SENT:
        draw_title("Messages");
        /* Centred confirmation banner */
        ui_fb_rect(0, 18, NOKIA_LCD_WIDTH, 11);
        ui_fb_text_inv(3, 20, "  Message sent!  ");
        draw_softkeys("", "OK");
        break;
    }
}

static void msg_on_key(nokia_key_t key)
{
    switch (state) {

    case MS_INBOX:
        switch (key) {
        case KEY_UP:    if (inbox_cursor > 0) inbox_cursor--;            break;
        case KEY_DOWN:  if (inbox_cursor < INBOX_COUNT - 1) inbox_cursor++; break;
        case KEY_OK:    view_idx = inbox_cursor; state = MS_VIEW;        break;
        case KEY_LEFT:  compose_reset(); state = MS_COMPOSE;             break;
        case KEY_RIGHT: ui_back();                                        break;
        default:        break;
        }
        break;

    case MS_VIEW:
        /* Any nav key returns to inbox */
        state = MS_INBOX;
        break;

    case MS_COMPOSE:
        switch (key) {
        case KEY_RIGHT:
            /* SEND  -  commit any pending T9 char, then go to Sent screen */
            t9_commit();
            sent_ticks = 0;
            state      = MS_SENT;
            break;
        case KEY_LEFT:
            /* DEL / Cancel */
            if (t9_key >= 0) {
                /* cancel pending T9 char without committing */
                t9_key   = -1;
                t9_tap   = 0;
                t9_timer = 0;
            } else if (compose_len > 0) {
                compose_buf[--compose_len] = '\0';
            } else {
                state = MS_INBOX;
            }
            break;
        case KEY_OK:
            /* Commit current pending T9 char */
            t9_commit();
            break;
        default:
            break;
        }
        break;

    case MS_SENT:
        state = MS_INBOX;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Exported applet descriptor                                           */
/* ------------------------------------------------------------------ */
const applet_t messages_applet = {
    .init   = msg_init,
    .tick   = msg_tick,
    .render = msg_render,
    .on_key = msg_on_key,
};
