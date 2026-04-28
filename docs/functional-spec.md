# Nokia 3310 UI Framework — Functional Specification

## 1. Purpose and Scope

The Nokia 3310 UI Framework is a declarative, data-driven UI engine for a Nokia 3310 simulator running on Zephyr RTOS v3.6.0 targeting the nRF52840 SoC, executed inside Renode. The framework provides screen definition, widget rendering, navigation stack management, and key-event routing. Application developers describe screens and transitions entirely as `static const` C data structures in `screens.c`; no application logic runs inside the engine. The framework does **not** provide: animation, timers, persistence, localization, input debouncing, or any hardware peripheral driver beyond writing `nokia_fb` in RAM. It has no knowledge of Bluetooth, serial, or any other peripheral.

---

## 2. System Architecture

```
+------------------+
|     main.c       |  Zephyr main loop (100 ms tick)
|  nokia_keys_raw  |  memory-mapped key byte, polled each iteration
+--------+---------+
         |  ui_init / ui_inject_key / ui_tick
+--------v---------+
|  ui_framework.c  |  navigation stack, key routing, widget rendering
+--------+---------+
         |  writes
+--------v---------+
|   nokia_fb[][]   |  6×84-byte framebuffer in RAM (fixed link-time address)
+------------------+
         ^  sysbus ReadBytes <address> 504
         |
+--------+---------+         +---------------------+
|     Renode       |<--------| lcd_viewer.py        |
| nrf52840 + ELF   | socket  | Tkinter display      |
+------------------+ :1234   | --address <nokia_fb> |
                             +---------------------+
```

`nokia_fb` is declared `__attribute__((used))` at file scope in `ui_framework.c`. Its address is fixed at link time by the linker script. `lcd_viewer.py` must be invoked with `--address` set to that same address. The `nokia_keys_raw` byte in `main.c` is similarly `__attribute__((used))` and `volatile`; Renode writes to it via `sysbus WriteByteToAddress`.

---

## 3. Hardware & Display Constraints

### 3.1 LCD

- Resolution: **84 × 48 pixels**, monochrome.
- Controller format: PCD8544 (also used by the Nokia 5110).
- Display colors rendered by `lcd_viewer.py`: dark green `#1b2f1b` (pixel on) on light green `#9bcf9b` (pixel off).

### 3.2 Framebuffer Memory Layout

```c
uint8_t nokia_fb[6][84];   // nokia_fb[page][column]
```

- **6 pages**, numbered 0–5. Each page covers 8 pixel rows.
- Page `p` covers pixel rows `p*8` through `p*8+7`.
- Each byte `nokia_fb[p][x]` encodes 8 vertically stacked pixels in column `x`.
- **Bit 0 (LSB) = topmost row of the page** (i.e., pixel row `p*8`).
- **Bit 7 = bottom row of the page** (pixel row `p*8+7`).
- Setting pixel `(x, y)`: `nokia_fb[y/8][x] |= (1 << (y%8))`.
- Total framebuffer size: `6 × 84 = 504 bytes`.

`lcd_viewer.py` reads exactly 504 bytes (`LCD_BYTES = LCD_WIDTH * LCD_HEIGHT // 8`) starting at the address of `nokia_fb`.

### 3.3 Keys

| Constant    | Bit value | Physical key       |
|-------------|-----------|--------------------|
| `KEY_NONE`  | `0x00`    | (no key)           |
| `KEY_LEFT`  | `0x01`    | Left softkey       |
| `KEY_RIGHT` | `0x02`    | Right softkey      |
| `KEY_UP`    | `0x04`    | Up                 |
| `KEY_DOWN`  | `0x08`    | Down               |
| `KEY_OK`    | `0x10`    | Centre / OK        |

All values fit in one byte; only one key is processed per 100 ms tick.

### 3.4 Key Injection

`nokia_keys_raw` is a `volatile uint8_t` at file scope in `main.c`. Each main-loop iteration reads it; if non-zero, clears it to zero, then calls `ui_inject_key((nokia_key_t)k)`. Key injection is **polled**, not interrupt-driven. A key written between two loop iterations is processed on the next iteration (up to 100 ms latency). Injecting from Renode:

```
sysbus WriteByteToAddress <nokia_keys_raw_address> 0x01   # KEY_LEFT
```

---

## 4. Screen Layout Zones

```
 x:  0                                                          83
 y:  0 +--------------------------------------------------------+
       | [signal bars]              [battery icon]              |  STATUS BAR
    13 +--------------------------------------------------------+  <- separator (fb_hline(13))
    14 |                                                        |
       |               CONTENT / MENU AREA                     |
    38 |                                                        |
    39 +--------------------------------------------------------+  <- separator (fb_hline(39))
    40 |                                                        |  SOFTKEY BAR
       | [left label]                          [right label]   |  labels at y=41
    47 +--------------------------------------------------------+
```

| Zone         | Pixel rows | Notes                                        |
|--------------|------------|----------------------------------------------|
| Status bar   | 0–13       | Signal bars + battery, separator at y=13     |
| Content area | 14–38      | Free for widgets; menu list starts at y=15   |
| Softkey bar  | 39–47      | Separator at y=39, text at y=41              |

---

## 5. The Declarative UI Model

The framework enforces a strict separation of data and logic. A developer defines the complete UI by editing **only `screens.c`**:

1. An `enum` of screen IDs (`SCR_IDLE`, `SCR_MAIN_MENU`, …, `SCR_COUNT`).
2. Per-screen `static const widget_t` arrays describing what to render.
3. Per-menu `static const menu_item_t` arrays describing selectable items.
4. A `const screen_def_t g_screens[SCR_COUNT]` table wiring together widgets, items, and key actions.
5. `const int g_screen_count = SCR_COUNT;`

No functions are written per screen (unless `CALL(fn)` is used for side effects). The engine in `ui_framework.c` is not modified. All structures are `static const`, allocated in flash.

---

## 6. Screen Types

### 6.1 `SCREEN_STATIC`

A static content screen. All five key presses are routed to the corresponding `on_*` field in `screen_def_t`:

| Key pressed | Field consulted |
|-------------|-----------------|
| `KEY_LEFT`  | `.on_left`      |
| `KEY_RIGHT` | `.on_right`     |
| `KEY_UP`    | `.on_up`        |
| `KEY_DOWN`  | `.on_down`      |
| `KEY_OK`    | `.on_ok`        |

If an `on_*` field is `NO_ACT` (i.e., `.type == ACT_NONE`), the key press is silently ignored. Fields not explicitly initialised default to zero, which equals `NO_ACT`.

### 6.2 `SCREEN_MENU`

A scrollable list screen. Per-entry state (`cursor`, `scroll`) is held in the navigation stack entry for this screen. Key routing:

| Key         | Behaviour                                                                   |
|-------------|-----------------------------------------------------------------------------|
| `KEY_UP`    | Decrement cursor by 1 (floor 0); if cursor falls below scroll, scroll--     |
| `KEY_DOWN`  | Increment cursor by 1 (ceiling `item_count-1`); if cursor >= scroll+3, scroll++ |
| `KEY_OK`    | Execute `items[cursor].on_select`                                           |
| `KEY_LEFT`  | Execute `items[cursor].on_select` (same as OK)                              |
| `KEY_RIGHT` | Execute `scr->on_right` (not item-driven; typically `BACK()`)               |

The `on_left`, `on_up`, `on_down`, and `on_ok` fields of the screen descriptor are **not consulted** for a `SCREEN_MENU`. Only `.on_right` is used.

**Visible item count:** Always 3 (`MENU_VISIBLE_ITEMS`). If `item_count < 3`, only `item_count` items are shown.

**Scroll invariants:**
- `0 <= scroll <= cursor <= item_count-1`
- `cursor < scroll + MENU_VISIBLE_ITEMS`
- When the cursor leaves the visible window, scroll adjusts by exactly 1 to restore the invariant.

---

## 7. Widget Catalog

Each widget is a `widget_t` struct with fields `type`, `x`, `y`, `scale`, `text`, `text2`. Unused fields are zero.

### 7.1 `WID_LABEL`

Renders left-aligned text starting at pixel `(x, y)`. If `scale == 1`, uses `fb_draw_string`; if `scale > 1`, uses `fb_draw_string_scaled`. Character advance is `6 * scale` pixels (5-pixel glyph + 1-pixel gap, each dimension scaled). Characters outside ASCII 0x20–0x7E are substituted with `'?'`.

- **Fields:** `x`, `y`, `scale`, `text`
- **Macro:** `LABEL(px, py, str)` → scale=1; `LABEL_SCALED(px, py, str, sc)` → scale=sc
- **Example:** `LABEL_SCALED(15, 18, "12:34", 2)` draws "12:34" at double size from x=15, y=18.

### 7.2 `WID_LABEL_CENTERED`

Renders text centred horizontally at pixel row `y`. Centring formula: `x = (84 - len*6) / 2`. String length is clamped to `84/6 = 14` characters before computing `x`.

- **Fields:** `y`, `text` (`x` and `scale` are ignored)
- **Macro:** `LABEL_CENTER(py, str)`
- **Example:** `LABEL_CENTER(30, "LORA")`

### 7.3 `WID_STATUS_BAR`

Renders the signal bar group and battery icon. No fields are used from the widget struct; state is read from global `g_signal` and `g_battery`. See §11 for exact geometry.

- **Macro:** `STATUS_BAR()`
- **Placement:** Conventionally the first widget in any screen that uses a status bar.

### 7.4 `WID_SOFTKEY_BAR`

Renders a full-width separator at y=39 and two text labels at y=41. Left label starts at x=1. Right label is right-justified: `x = 84 - strlen(right)*6 - 1` (floor 0). Empty or NULL strings are not drawn.

- **Fields:** `text` (left label), `text2` (right label)
- **Macro:** `SOFTKEY_BAR(l, r)` — `y` is hardcoded to 40 in the macro but the separator is drawn at y=39 and text at y=41 by `render_softkey_bar`.
- **Example:** `SOFTKEY_BAR("MENU", "NAMES")`

### 7.5 `WID_HLINE`

Draws a full-width horizontal line (all 84 pixels set) at row `y`.

- **Fields:** `y`
- **Macro:** `HLINE(py)`
- **Example:** `HLINE(9)`

### 7.6 `WID_MENU_LIST`

Renders the scrollable item list for a `SCREEN_MENU`. Must only be used in a `SCREEN_MENU` screen; on a `SCREEN_STATIC` screen `scr->items` will be NULL and nothing is drawn. Renders at most `min(item_count, 3)` items. The highlighted item (at `cursor`) is drawn with a filled black rectangle behind it (`fb_fill_rect(0, item_y-1, 84, 9)`) and the label XOR'd white. Non-highlighted items are drawn normally. Scroll arrows (3-pixel triangles) appear at x=79 when items exist above or below the visible window.

- **Fields:** `x` (label indent, fixed at 2 by macro), `y` (top of first item, fixed at 15 by macro)
- **Item row height:** 9 pixels (`MENU_ITEM_H`)
- **Macro:** `MENU_LIST()`

---

## 8. Action System

An `action_t` is a struct with three fields: `type` (`action_type_t`), `screen_id` (int), and `fn` (function pointer `void(*)(void)`).

| Type       | Used fields  | Behaviour                                       |
|------------|--------------|-------------------------------------------------|
| `ACT_NONE` | —            | No-op                                           |
| `ACT_GOTO` | `screen_id`  | Push `screen_id` onto navigation stack          |
| `ACT_BACK` | —            | Pop navigation stack                            |
| `ACT_CALL` | `fn`         | Invoke `fn()` with no arguments, no return value |

**Constraints:**
- `ACT_GOTO`: `screen_id` must be in `[0, g_screen_count-1]`. Out-of-range values are silently ignored by `exec_action`.
- `ACT_CALL`: `fn` must be non-NULL; a NULL pointer is silently ignored.
- `CALL(fn)` requires `fn` to be a function pointer constant valid at file-scope static initialisation time (C99 restriction).

**Macro syntax** (all expand to brace-enclosed initialiser lists):

```c
#define GOTO(id)   { ACT_GOTO, (id), 0    }
#define BACK()     { ACT_BACK, 0,    0    }
#define CALL(fn_)  { ACT_CALL, 0,    (fn_) }
#define NO_ACT     { ACT_NONE, 0,    0    }
```

These macros **must be used only as struct member initialisers or array element initialisers**. They cannot be used in assignment expressions because C99 compound literals in that position require a cast.

---

## 9. Navigation Stack

The stack is a fixed array of `nav_entry_t` (`SCREEN_STACK_DEPTH = 8` entries). Each entry stores:

```c
typedef struct {
    int screen_id;   // which screen
    int cursor;      // menu cursor position
    int scroll;      // menu scroll offset
} nav_entry_t;
```

**`ui_init`** seeds `nav_stack[0]` with `{initial, 0, 0}` and sets `nav_depth = 0`.

**`ACT_GOTO(id)`:** Increments `nav_depth`, writes `{id, 0, 0}` into `nav_stack[nav_depth]`. Silently ignored if `nav_depth` is already `SCREEN_STACK_DEPTH - 1` (stack full) or if `id` is out of range. The new screen starts with cursor and scroll at zero.

**`ACT_BACK`:** Decrements `nav_depth` if `nav_depth > 0`. If `nav_depth == 0` (root screen), BACK is a no-op. The screen below retains its cursor and scroll from when it was last active.

**`ui_tick` / `ui_inject_key`** always operate on `nav_stack[nav_depth]`.

---

## 10. Rendering Pipeline

Each call to `ui_tick()` executes synchronously:

1. **`fb_clear()`** — `memset(nokia_fb, 0x00, 504)` clears all pixels.
2. **Widget loop** — iterates `scr->widgets[0..widget_count-1]` in order, dispatching each to its renderer.
3. After `ui_tick()` returns, `nokia_fb` contains the fully rendered frame ready for `lcd_viewer.py` to read.

`ui_tick()` is called once per main-loop iteration, which sleeps for `K_MSEC(100)` (100 ms) via `k_sleep`. Rendering is therefore approximately **10 frames per second**.

**Font format:** The embedded `font_5x7` table covers ASCII 0x20–0x7E (95 glyphs). Each glyph is 5 bytes; each byte is one column; **bit 0 is the top pixel row** of that column, bit 6 is the bottom. Characters advance by 6 pixels horizontally (implicit 1-pixel gap after each glyph). Scaled text advances by `6 * scale` pixels per character.

---

## 11. Status Bar Rendering

### 11.1 Signal Bars

Five bars are drawn, bottom-aligned. Bar `i` (0–4):

- **X position:** `x = 2 + i*3`
- **Height:** `h = 2 + i*2` pixels (bar 0: 2px, bar 1: 4px, bar 2: 6px, bar 3: 8px, bar 4: 10px)
- **Bottom pixel row:** y=11 (the bar occupies rows `11-h+1` through `11`)
- **Filled** (bar index < `g_signal`): all `h` pixels in the column are set.
- **Hollow** (bar index >= `g_signal`): only the top pixel (`y = 11-h+1`) and bottom pixel (`y = 11`) are set.

```
g_signal=3 (three filled, two hollow):

 y=3  .  .  .  .  #      bar 4 hollow (top pixel only shown here)
 y=5  .  .  .  #  .      bar 3 hollow (top pixel)
 y=7  .  .  #  #  .
 y=8  .  #  #  #  .
 y=9  #  #  #  #  .
 y=10 #  #  #  #  #      bar 4 hollow (bottom pixel)
 y=11 #  #  #  #  #      bottom row — all bars
      x=2 5  8  11 14
```

### 11.2 Battery Icon

- **Position:** `bx = 71` (`LCD_WIDTH - 13`), `by = 2`
- **Outline:** 10 wide × 7 tall rectangle (rows `by..by+6`, columns `bx..bx+9`)
- **Terminal nub:** single-pixel-wide nub at `(bx+10, by+2)`, `(bx+10, by+3)`, `(bx+10, by+4)`
- **Fill:** 5-row-tall filled rectangle inside the outline (`fy = by+1..by+5`, `fx = bx+1..bx+fw`)

Fill widths by battery level:

| `g_battery` | Fill width (`fw`) |
|-------------|-------------------|
| 0           | 0 px (empty)      |
| 1           | 2 px              |
| 2           | 5 px              |
| 3           | 8 px (full)       |

A separator line is drawn at y=13 (`fb_hline(13)`) after both icons.

---

## 12. API Reference

### `void ui_init(const screen_def_t *screens, int count, int initial)`

**Preconditions:** `screens` is a non-NULL pointer to an array of `count` valid `screen_def_t` entries. `initial` is in `[0, count-1]`. The array must remain valid for the lifetime of the process (static storage).

**Postconditions:** Navigation stack reset to depth 0 with `screen_id=initial`, `cursor=0`, `scroll=0`. `g_screens` and `g_screen_count` updated. No rendering occurs.

**Side effects:** Overwrites all prior navigation state. Safe to call once at startup only.

**Invalid input:** No bounds checking on `initial` or `count`. Passing an out-of-range `initial` or NULL `screens` is undefined behaviour.

---

### `void ui_tick(void)`

**Preconditions:** `ui_init` has been called.

**Postconditions:** `nokia_fb` contains a fully rendered frame for the current screen.

**Side effects:** Overwrites entire `nokia_fb`. Reads `g_signal`, `g_battery`, and navigation stack state. Purely a rendering function; does not modify navigation state.

---

### `void ui_inject_key(nokia_key_t key)`

**Preconditions:** `ui_init` has been called. `key` is one of the `KEY_*` constants.

**Postconditions:** The action corresponding to `key` on the current screen has been executed (which may modify `nav_depth`, `cursor`, or `scroll`). No rendering occurs.

**Side effects:** May modify `nav_depth`, `nav_stack[nav_depth].cursor`, or `nav_stack[nav_depth].scroll`. May call `fn()` for `ACT_CALL` actions.

**Invalid input:** `KEY_NONE` (0x00) reaches the default branch of the inner switch and is silently ignored. Any bit pattern not equal to a defined `KEY_*` constant is similarly ignored.

---

### `void ui_set_signal(int level)`

Sets `g_signal`. Input is clamped: values < 0 become 0; values > 5 become 5. Takes effect on the next `ui_tick()`. Default is 5.

---

### `void ui_set_battery(int level)`

Sets `g_battery`. Input is clamped: values < 0 become 0; values > 3 become 3. Takes effect on the next `ui_tick()`. Default is 3.

---

### `int ui_current_screen_id(void)`

Returns `nav_stack[nav_depth].screen_id`. Valid only after `ui_init`. Primarily for unit tests.

---

### `int ui_current_menu_cursor(void)`

Returns `nav_stack[nav_depth].cursor`. Returns 0 on `SCREEN_STATIC` screens (field is present but never modified by key handlers).

---

### `int ui_current_menu_scroll(void)`

Returns `nav_stack[nav_depth].scroll`. Returns 0 on `SCREEN_STATIC` screens.

---

## 13. Macro Reference

| Macro | Expansion | Usage context | Constraints |
|---|---|---|---|
| `GOTO(id)` | `{ ACT_GOTO, (id), 0 }` | `action_t` struct member init | `id` must be a valid screen enum value |
| `BACK()` | `{ ACT_BACK, 0, 0 }` | `action_t` struct member init | — |
| `CALL(fn_)` | `{ ACT_CALL, 0, (fn_) }` | `action_t` struct member init | `fn_` must be a `void(*)(void)` constant |
| `NO_ACT` | `{ ACT_NONE, 0, 0 }` | `action_t` struct member init | Equivalent to zero-initialisation |
| `LABEL(px,py,str)` | `{ WID_LABEL, px, py, 1, str, 0 }` | `widget_t` array element | — |
| `LABEL_SCALED(px,py,str,sc)` | `{ WID_LABEL, px, py, sc, str, 0 }` | `widget_t` array element | `sc >= 1` |
| `LABEL_CENTER(py,str)` | `{ WID_LABEL_CENTERED, 0, py, 1, str, 0 }` | `widget_t` array element | — |
| `STATUS_BAR()` | `{ WID_STATUS_BAR, 0, 0, 1, 0, 0 }` | `widget_t` array element | At most once per screen |
| `SOFTKEY_BAR(l,r)` | `{ WID_SOFTKEY_BAR, 0, 40, 1, l, r }` | `widget_t` array element | At most once per screen |
| `HLINE(py)` | `{ WID_HLINE, 0, py, 1, 0, 0 }` | `widget_t` array element | — |
| `MENU_LIST()` | `{ WID_MENU_LIST, 2, 15, 1, 0, 0 }` | `widget_t` array element | Only valid in `SCREEN_MENU` screens |
| `ARRAY_SIZE(arr)` | `sizeof(arr)/sizeof((arr)[0])` | Any array size expression | Argument must be an array, not a pointer |

All brace-list macros must appear in initialiser context only; they cannot be assigned to an existing variable.

---

## 14. Adding a New Screen

**Step 1 — Add the screen ID to the enum** in `screens.c`:

```c
enum {
    SCR_IDLE = 0,
    SCR_MAIN_MENU,
    SCR_ABOUT,     // <- new
    SCR_COUNT,
};
```

`SCR_COUNT` must remain the last entry. IDs must be contiguous from 0.

**Step 2 — Declare the widget array:**

```c
static const widget_t about_widgets[] = {
    STATUS_BAR(),
    LABEL_CENTER(1, "About"),
    HLINE(9),
    LABEL_CENTER(22, "v1.0.0"),
    SOFTKEY_BAR("", "BACK"),
};
```

**Step 3 — For a menu screen, declare the item array** (skip for `SCREEN_STATIC`):

```c
static const menu_item_t about_menu_items[] = {
    { "Version", NO_ACT },
    { "Credits", GOTO(SCR_CREDITS) },
};
```

**Step 4 — Add the entry to `g_screens[]`:**

```c
[SCR_ABOUT] = {
    .name         = "About",
    .type         = SCREEN_STATIC,
    .widgets      = about_widgets,
    .widget_count = ARRAY_SIZE(about_widgets),
    .on_right     = BACK(),
},
```

`g_screens` is declared `const screen_def_t g_screens[SCR_COUNT]`; the new `SCR_ABOUT` slot is automatically included because `SCR_COUNT` increased. No changes to `ui_framework.c` or `main.c` are required.

**Step 5 — Navigate to the new screen** by adding `GOTO(SCR_ABOUT)` to an existing screen's action or menu item.

---

## 15. Adding a New Widget Type

**Step 1 — Add the enum value** to `widget_type_t` in `ui_framework.h`:

```c
typedef enum {
    WID_LABEL,
    WID_LABEL_CENTERED,
    WID_STATUS_BAR,
    WID_SOFTKEY_BAR,
    WID_HLINE,
    WID_MENU_LIST,
    WID_PROGRESS_BAR,   // <- new
} widget_type_t;
```

**Step 2 — Add fields if needed.** `widget_t` already provides `x`, `y`, `scale`, `text`, `text2`. Repurpose unused fields (e.g., use `scale` as the progress value 0–100) before extending the struct.

**Step 3 — Add a rendering macro** in `ui_framework.h`:

```c
#define PROGRESS_BAR(px, py, val) { WID_PROGRESS_BAR, (px), (py), (val), 0, 0 }
```

**Step 4 — Add a `case` to the widget switch** in `render_screen()` in `ui_framework.c`:

```c
case WID_PROGRESS_BAR: {
    int fill = (w->scale * 80) / 100;   // scale holds 0-100 value
    fb_fill_rect(w->x, w->y, fill, 5);
    for (int i = 0; i < 80; i++) {
        fb_set_pixel(w->x + i, w->y);
        fb_set_pixel(w->x + i, w->y + 4);
    }
    fb_set_pixel(w->x, w->y + 2);
    fb_set_pixel(w->x + 79, w->y + 2);
    break;
}
```

No other files require modification.

---

## 16. Known Constraints and Invariants

- **Screen IDs must be contiguous from 0 to `SCR_COUNT-1`.** The engine uses `screen_id` as a direct array index into `g_screens`. Gaps cause out-of-bounds access.

- **Navigation stack overflow is silently ignored.** If `nav_depth == SCREEN_STACK_DEPTH - 1` (7) and `GOTO` is executed, the navigation is dropped. The stack does not wrap. Maximum navigable depth is 8 levels.

- **`ACT_BACK` at root is a no-op.** `nav_depth == 0` is the base case; `BACK()` does not underflow.

- **`CALL(fn)` requires a function pointer constant at file scope.** The action struct is `static const`; `fn` must be resolvable at compile/link time. Lambda-style local functions are not supported in C99.

- **Macros expand to brace-list initialisers.** They cannot appear on the right-hand side of an assignment operator or inside a function argument that expects a scalar. Always use them inside `= { ... }` initialisers.

- **`nokia_fb` address is fixed at link time.** The `--address` argument passed to `lcd_viewer.py` must exactly match the address the linker assigned to `nokia_fb`. This address can be found by running `nm zephyr.elf | grep nokia_fb` after building. The Renode script in `run.resc` loads the ELF but does not pass `nokia_fb`'s address automatically to `lcd_viewer.py`; the operator must supply it manually.

- **`nokia_keys_raw` is polled, not interrupt-driven.** A key written by Renode is processed on the next main-loop iteration (up to 100 ms later). Writing a second key before the first is consumed overwrites it; only one key per tick is processed.

- **String rendering is not clipped to zones.** `fb_draw_string` and `fb_draw_string_scaled` will draw pixels anywhere on screen, including over the status bar or softkey bar, if `x` or `y` positions are chosen incorrectly. Clipping is only applied at the pixel level (pixels outside `[0,83]x[0,47]` are silently discarded by `fb_set_pixel`).

- **Centered text is clamped to 14 characters.** `fb_draw_string_centered` clamps `len` to `LCD_WIDTH / 6 = 14` before computing the centre offset. Characters beyond position 14 are not rendered.

- **`g_signal` defaults to 5, `g_battery` defaults to 3** (full bars, full battery) at startup. Call `ui_set_signal` / `ui_set_battery` before the first `ui_tick()` to change initial values.
