# Nokia 3310 UI Framework — Test Specification

## 1. Goals

The test suite verifies four properties of `ui_framework.c`. **Navigation correctness** ensures that `ACT_GOTO` pushes a new entry onto the navigation stack, `ACT_BACK` pops it, BACK at depth 0 is a no-op, and the stack silently clamps rather than overflowing. **Rendering pixel accuracy** confirms that after one call to `ui_tick()` specific bit positions in `nokia_fb` are set or clear; each test derives a single expected coordinate from the rendering math so that any off-by-one in the framebuffer geometry or font indexing produces a failure. **Input validation clamping** checks that `ui_set_signal` and `ui_set_battery` discard out-of-range values before they can corrupt the status-bar renderer. **Callback dispatch** verifies that `ACT_CALL` invokes the registered function pointer exactly once per key event, with no double-fire or loss.

---

## 2. Repository Layout

```
firmware/tests/
├── Makefile                  # Standalone host build (cc, no Zephyr)
└── ui/
    ├── CMakeLists.txt        # Zephyr application manifest; pulls in ui_framework.c
    ├── prj.conf              # Enables CONFIG_ZTEST, CONFIG_ZTEST_NEW_API, CONFIG_NEWLIB_LIBC
    └── src/
        └── test_main.c       # All 28 test functions; dual-mode entry point
```

`test_main.c` compiles in two modes selected by the preprocessor:

- **Standalone** (`CONFIG_ZTEST` not defined): compiled with `cc -std=c99`. The `main()` function calls every test function in sequence, accumulating `_pass`/`_fail` counters and printing a summary line. Failures are non-fatal; execution continues to the next test.
- **Ztest** (`CONFIG_ZTEST` defined by Zephyr): each test function is registered with `ZTEST(suite, name)`. The Ztest runner owns execution; a failing `zassert_*` aborts the current test case but the suite continues.

Both modes compile the same 28 `static void test_*()` functions. There is no test-only source file other than `test_main.c`; `ui_framework.c` is compiled directly into the test binary (no mocking layer).

---

## 3. Running the Tests

### 3a. Standalone host (macOS / Linux — no Zephyr required)

```sh
make -C firmware/tests test
```

The Makefile invokes `cc` twice — once for `ui_framework.c` and once for `test_main.c` — links them into `firmware/tests/_build/host_test`, and runs it immediately. No environment variables or SDK are needed. Expected output on a clean pass:

```
37/37 tests passed
```

The count of 37 is the total number of `TCHECK` / `TCHECK_EQ` assertions across all test functions, not the number of test functions (28). On failure the line preceding the summary identifies the source file, line number, and expression:

```
FAIL ui/src/test_main.c:374  pixel_at(0, 5)
36/37 tests passed  (1 FAILED)
```

The process exits with code 1 if any assertion fails, which allows CI to detect failures without parsing output.

To clean intermediate objects:

```sh
make -C firmware/tests clean
```

### 3b. Zephyr `native_sim` (requires `west` and the Zephyr SDK)

Build and run in-tree:

```sh
west build -b native_sim firmware/tests/ui
west build -t run
```

Run all suites via Twister:

```sh
west twister -T firmware/tests
```

Twister discovers `firmware/tests/ui` through `prj.conf` (`CONFIG_ZTEST=y`) and reports per-suite pass/fail. The `native_sim` board executes the POSIX host runner; no hardware is required.

---

## 4. Test Screen Definitions

| ID | `enum` constant | Screen type | Widgets | Key bindings |
|----|----------------|-------------|---------|--------------|
| 0 | `SCR_HOME` | `SCREEN_STATIC` | `HLINE(25)`, `LABEL(0,5,"Home")`, `SOFTKEY_BAR("GO","B")` | LEFT → `GOTO(SCR_MENU)`, RIGHT → `BACK()` |
| 1 | `SCR_MENU` | `SCREEN_MENU` | `MENU_LIST()`, `SOFTKEY_BAR("OK","BACK")` | RIGHT → `BACK()`; UP/DOWN/OK/LEFT managed automatically |
| 2 | `SCR_DETAIL` | `SCREEN_STATIC` | `LABEL_CENTER(20,"Detail")`, `SOFTKEY_BAR("","BACK")` | RIGHT → `BACK()`, OK → `CALL(test_callback)` |
| 3 | `SCR_STATUS` | `SCREEN_STATIC` | `STATUS_BAR()` | (none bound) |
| 4 | `SCR_LOOP` | `SCREEN_STATIC` | `LABEL(0,0,"X")` | LEFT → `GOTO(SCR_LOOP)` (self-loop) |

`menu_items[4]` attached to `SCR_MENU` all have `GOTO(SCR_DETAIL)` as their `on_select` action.

**Why each screen exists:**

- **SCR_HOME** — the natural root of the navigation stack. Tests that BACK at depth 0 is a no-op use `on_right = BACK()` here; tests that HLINE and LABEL rendering work use the known widgets on this screen.
- **SCR_MENU** — the only `SCREEN_MENU` in the fixture. All eight `menu_nav` tests and the menu-highlight rendering test require a live `SCREEN_MENU` with a multi-item list. Having exactly 4 items with a 3-item visible window creates a scroll boundary at `cursor == 3` without needing a larger item set.
- **SCR_DETAIL** — reached by navigating from `SCR_MENU`. Hosts `CALL(test_callback)` so `call_action` tests can verify callback dispatch without adding a fifth key binding to another screen. Also provides a `LABEL_CENTER` widget for the centered-label rendering test.
- **SCR_STATUS** — contains only `STATUS_BAR()` with no other widgets, which means the framebuffer after `ui_tick()` holds exactly the status-bar pixels. All four validation tests and all four signal/battery rendering tests navigate here so that pixel assertions are unambiguous (no other widget can set the probed coordinate).
- **SCR_LOOP** — its `on_left = GOTO(SCR_LOOP)` makes it push a new entry pointing to itself on every LEFT key. Sending 20 LEFT key events (more than `SCREEN_STACK_DEPTH = 8`) exercises the stack-overflow clamp path without the test having to know the internal depth constant.

---

## 5. Test Suite Catalog

### Suite `navigation` — 5 tests, 6 assertions

| Test | Description | Assertion |
|------|-------------|-----------|
| `test_nav_init` | After `ui_init(…, SCR_HOME)`, the current screen is `SCR_HOME`. | `ui_current_screen_id() == SCR_HOME` |
| `test_nav_goto` | LEFT on `SCR_HOME` executes `GOTO(SCR_MENU)` and transitions to `SCR_MENU`. | `ui_current_screen_id() == SCR_MENU` |
| `test_nav_back` | LEFT then RIGHT navigates HOME→MENU then MENU→HOME via `BACK()`. | `id == SCR_MENU` after LEFT; `id == SCR_HOME` after RIGHT |
| `test_nav_back_at_root_is_noop` | RIGHT on `SCR_HOME` (stack depth 0) leaves the screen unchanged. | `ui_current_screen_id() == SCR_HOME` |
| `test_nav_stack_overflow_clamped` | Twenty LEFT presses on `SCR_LOOP` (depth limit 8) do not crash; screen remains `SCR_LOOP`. | `ui_current_screen_id() == SCR_LOOP` |

### Suite `menu_nav` — 8 tests, 13 assertions

| Test | Description | Assertion |
|------|-------------|-----------|
| `test_menu_cursor_moves_down` | KEY_DOWN increments cursor from 0→1→2 on successive presses. | `cursor == 0`, then `1`, then `2` |
| `test_menu_cursor_clamps_at_top` | KEY_UP when cursor is already 0 leaves cursor at 0 (no underflow to −1). | `cursor == 0` before and after UP |
| `test_menu_cursor_clamps_at_bottom` | Ten KEY_DOWN presses on a 4-item list clamp cursor at 3. | `cursor == 3` |
| `test_menu_scroll_follows_cursor` | Three KEY_DOWN presses advance cursor to 3 and set scroll to 1 (cursor exits the 3-item window). | `cursor == 3 && scroll == 1` |
| `test_menu_scroll_up_follows_cursor` | After scrolling down (scroll=1), three KEY_UP presses return cursor to 0 and scroll to 0. | `cursor == 0 && scroll == 0` |
| `test_menu_ok_selects_item` | KEY_OK on `SCR_MENU` with cursor=0 executes `items[0].on_select = GOTO(SCR_DETAIL)`. | `ui_current_screen_id() == SCR_DETAIL` |
| `test_menu_left_selects_item` | KEY_LEFT in `SCREEN_MENU` is synonymous with KEY_OK — it selects the highlighted item. | `ui_current_screen_id() == SCR_DETAIL` |
| `test_menu_right_is_back` | KEY_RIGHT in `SCREEN_MENU` routes to `scr->on_right`, not item selection. | `ui_current_screen_id() == SCR_HOME` |

### Suite `call_action` — 2 tests, 2 assertions

| Test | Description | Assertion |
|------|-------------|-----------|
| `test_call_action_fires_callback` | KEY_OK on `SCR_DETAIL` fires `test_callback` exactly once. | `call_count == 1` |
| `test_call_action_fires_each_press` | Three successive KEY_OK presses each fire the callback; `call_count` reaches 3. | `call_count == 3` |

### Suite `validation` — 4 tests, 4 assertions

| Test | Description | Assertion |
|------|-------------|-----------|
| `test_battery_clamps_negative` | `ui_set_battery(-5)` clamps to 0; no inner fill drawn; pixel (72,3) is clear. | `!pixel_at(72, 3)` |
| `test_battery_clamps_overflow` | `ui_set_battery(99)` clamps to 3 (max); inner fill drawn; pixel (72,3) is set. | `pixel_at(72, 3)` |
| `test_signal_clamps_negative` | `ui_set_signal(-1)` clamps to 0; all bars hollow; interior pixel (14,5) is clear. | `!pixel_at(14, 5)` |
| `test_signal_clamps_overflow` | `ui_set_signal(999)` clamps to 5 (max); all bars filled; top of tallest bar (14,2) is set. | `pixel_at(14, 2)` |

### Suite `rendering` — 9 tests, 12 assertions

| Test | Description | Assertion |
|------|-------------|-----------|
| `test_render_hline_sets_full_row` | `HLINE(25)` on `SCR_HOME` fills every pixel of row 25, leaving rows 24 and 26 clear. | `row_is_full(25)`, `row_is_clear(24)`, `row_is_clear(26)` |
| `test_render_softkey_separator` | `SOFTKEY_BAR` calls `fb_hline(39)`; row 39 is fully set. | `row_is_full(39)` |
| `test_render_label_sets_pixels` | `LABEL(0,5,"Home")` starts at (0,5); 'H' column 0 = `0x7F`, bit 0 = 1 → pixel (0,5) set. | `pixel_at(0, 5)` |
| `test_render_label_center_sets_pixels` | `LABEL_CENTER(20,"Detail")` draws non-zero pixels somewhere in the 84×7 band at y=20. | `region_has_any_pixel(0, 20, 84, 7)` |
| `test_render_menu_selection_inverts_row` | `MENU_LIST()` cursor=0 calls `fb_fill_rect(0, 14, 84, 9)`; row 14 is fully black. | `row_is_full(14)` |
| `test_render_signal_full` | Signal=5 fills bar i=4; top pixel at (14,2) is set. | `pixel_at(14, 2)` |
| `test_render_signal_zero` | Signal=0 renders all bars hollow; interior of tallest bar at (14,5) is clear. | `!pixel_at(14, 5)` |
| `test_render_battery_full` | Battery=3 fills inner rectangle; pixel (72,3) is set. | `pixel_at(72, 3)` |
| `test_render_battery_empty` | Battery=0 draws no fill; pixel (72,3) is clear. | `!pixel_at(72, 3)` |

---

## 6. Rendering Test Methodology

All rendering tests share a common pattern: call `ui_init(…)` to set the screen, call `ui_tick()` to execute one render pass (which calls `fb_clear()` then dispatches each widget), then inspect `nokia_fb` directly using one of the pixel helpers defined in `test_main.c`.

### Framebuffer layout

`nokia_fb[NOKIA_LCD_PAGES][NOKIA_LCD_WIDTH]` — 6 pages × 84 bytes. Each byte is a vertical strip of 8 pixels: bit `b` of `nokia_fb[p][x]` controls the pixel at column `x`, row `p*8 + b`. The PCD8544 uses the same layout natively, so the framebuffer can be copied to the LCD without format conversion.

### Pixel helpers

```c
static int pixel_at(int x, int y)
{
    if (x < 0 || x >= NOKIA_LCD_WIDTH || y < 0 || y >= NOKIA_LCD_HEIGHT)
        return 0;
    return (nokia_fb[y / 8][x] >> (y % 8)) & 1;
}

static int row_is_full(int y)      // true iff every x in 0..83 has pixel_at(x,y)==1
static int row_is_clear(int y)     // true iff every x in 0..83 has pixel_at(x,y)==0
static int region_has_any_pixel(int x0, int y0, int w, int h)
                                   // true iff any pixel in the rectangle [x0,x0+w) × [y0,y0+h) is set
```

`pixel_at` bounds-checks before indexing, so probing outside the 84×48 region always returns 0 rather than reading uninitialised memory.

### Derivation of specific pixel coordinates

**Signal bars — why (14, 2) and (14, 5):**
The renderer loops `i = 0..4`. For bar `i`, `h = 2 + i*2` and `x = 2 + i*3`. The tallest bar is `i = 4`: `h = 10`, `x = 14`. Bars are drawn bottom-aligned at `bar_bot = 11`. When filled, pixels span `y = bar_bot − (h−1)` to `bar_bot` = `y = 2` to `y = 11`, so the topmost filled pixel is `(14, 2)`. When hollow, only the top pixel `(14, 2)` and the bottom pixel `(14, 11)` are set. The middle pixel `(14, 5)` is therefore clear in the hollow state, which is what the zero-signal tests assert. Using the interior pixel rather than an edge pixel ensures the hollow rendering is tested, not just the bar boundary.

**Battery icon — why (72, 3):**
`bx = LCD_WIDTH − 13 = 71`, `by = 2`. The inner fill loop runs `fy` from `by+1 = 3` to `by+5 = 7` and `fx` from `bx+1 = 72` up to `bx + 1 + fw − 1`. For level 3 (full), `fw = fill_w[3] = 8`, so `fx` spans 72..79. Pixel `(72, 3)` is the top-left corner of the fill region; it is set for any non-zero level with `fw ≥ 1`, i.e. levels 1–3. For level 0, `fw = 0` and the fill loop body is never entered, so `(72, 3)` remains clear.

**Menu highlight — why row 14:**
`MENU_LIST()` expands to `{ WID_MENU_LIST, 2, 15, 1, 0, 0 }` — `w->y = 15`. When `cursor == 0`, `idx == cursor`, so `item_y = w->y + 0 * 9 = 15`. The highlight call is `fb_fill_rect(0, item_y − 1, 84, 9)` = `fb_fill_rect(0, 14, 84, 9)`. This sets rows 14 through 22 fully black. `row_is_full(14)` is therefore the minimum assertion that verifies the highlight was drawn at the correct vertical position without depending on the text content of the item label.

**HLINE(25) — why row 25:**
`fb_hline(y)` sets `fb_set_pixel(x, y)` for every `x` in `0..83`. No other widget on `SCR_HOME` touches row 25, 24, or 26, making the adjacent-row assertions reliable guards against off-by-one errors in `w->y` interpretation.

**SOFTKEY_BAR — why row 39:**
`render_softkey_bar` unconditionally calls `fb_hline(39)` as its first action. The macro `SOFTKEY_BAR(l, r)` stores `y = 40` in the widget struct, but the separator is hardcoded one row above the label baseline.

**LABEL(0, 5, "Home") — why (0, 5):**
`fb_draw_string(0, 5, "Home", 0)` draws 'H' with `fb_draw_glyph(0, 5, glyph, 0)`. The font entry for 'H' (index `0x48 − 0x20 = 40`) is `{0x7F, 0x08, 0x08, 0x08, 0x7F}`. Column 0 is `0x7F = 0b0111_1111`; bit 0 = 1, so the glyph sets pixel `(x + 0, y + 0) = (0, 5)`.

---

## 7. Coverage Analysis

| Behavior | Covered | Notes |
|---|---|---|
| `ACT_GOTO` | ✅ | `test_nav_goto`, `test_menu_ok_selects_item` |
| `ACT_BACK` | ✅ | `test_nav_back` |
| `ACT_CALL` | ✅ | `test_call_action_fires_callback`, `test_call_action_fires_each_press` |
| `ACT_NONE` | ✅ (implicit) | Unbound keys on `SCR_STATUS` produce no navigation change |
| `SCREEN_STATIC` key routing | ✅ | LEFT/RIGHT on `SCR_HOME` and `SCR_DETAIL` |
| `SCREEN_MENU` UP/DOWN | ✅ | `test_menu_cursor_moves_down`, clamp tests |
| `SCREEN_MENU` OK | ✅ | `test_menu_ok_selects_item` |
| `SCREEN_MENU` LEFT (= OK) | ✅ | `test_menu_left_selects_item` |
| `SCREEN_MENU` RIGHT (= `on_right`) | ✅ | `test_menu_right_is_back` |
| Stack overflow clamp | ✅ | `test_nav_stack_overflow_clamped` |
| BACK at root | ✅ | `test_nav_back_at_root_is_noop` |
| Cursor clamp top | ✅ | `test_menu_cursor_clamps_at_top` |
| Cursor clamp bottom | ✅ | `test_menu_cursor_clamps_at_bottom` |
| Scroll window down | ✅ | `test_menu_scroll_follows_cursor` |
| Scroll window up | ✅ | `test_menu_scroll_up_follows_cursor` |
| Signal clamping | ✅ | negative and overflow (`test_signal_clamps_*`) |
| Battery clamping | ✅ | negative and overflow (`test_battery_clamps_*`) |
| `WID_HLINE` render | ✅ | `test_render_hline_sets_full_row` |
| `WID_SOFTKEY_BAR` separator | ✅ | `test_render_softkey_separator` |
| `WID_LABEL` pixel correctness | ✅ | `test_render_label_sets_pixels` (first pixel of 'H') |
| `WID_LABEL_CENTERED` centering | ⚠️ | Only checks region is non-empty; exact `x` not asserted |
| `WID_STATUS_BAR` signal filled | ✅ | `test_render_signal_full` |
| `WID_STATUS_BAR` signal hollow | ✅ | `test_render_signal_zero` |
| `WID_STATUS_BAR` battery filled | ✅ | `test_render_battery_full` |
| `WID_STATUS_BAR` battery empty | ✅ | `test_render_battery_empty` |
| `WID_MENU_LIST` selection highlight | ✅ | `test_render_menu_selection_inverts_row` |
| `WID_MENU_LIST` visibility (scroll offset) | ✅ (indirect) | scroll state verified via `ui_current_menu_scroll()` |
| `SCREEN_APPLET` init/tick/render/key routing | ❌ | No host-test coverage; known gap (see §8) |
| `ui_back()` from applet | ❌ | No host-test coverage; known gap (see §8) |
| `ui_fb_*` drawing primitives | ❌ | No host-test coverage; known gap (see §8) |
| Messages applet — inbox UP/DOWN scroll | ❌ | No test; see §13 |
| Messages applet — inbox OK → `MS_VIEW` transition | ❌ | No test; see §13 |
| Messages applet — inbox LEFT → `MS_COMPOSE` transition | ❌ | No test; see §13 |
| Messages applet — view: any key → `MS_INBOX` | ❌ | No test; see §13 |
| Messages applet — T9 multi-tap cycling | ❌ | No test; see §13 |
| Messages applet — T9 timeout auto-commit | ❌ | No test; see §13 |
| Messages applet — T9 uppercase toggle (`*`) | ❌ | No test; see §13 |
| Messages applet — T9 backspace (DEL) variants | ❌ | No test; see §13 |
| Messages applet — compose SEND → `MS_SENT` | ❌ | No test; see §13 |
| Messages applet — `MS_SENT` auto-dismiss | ❌ | No test; see §13 |
| `nokia_char_raw` cleared on applet `init()` | ❌ | No test; see §13 |

---

## 8. Known Gaps

- **Scaled labels (`LABEL_SCALED`)** — `fb_draw_string_scaled` is exercised only at `scale = 1` (normal). No pixel test covers `scale ≥ 2`; a bug in `fb_draw_glyph_scaled`'s inner `sx`/`sy` loops would go undetected.
- **Full character correctness for all 95 font glyphs** — only 'H' at a single position is sampled. Font data corruption for other glyphs is invisible to the test suite.
- **Softkey label text pixel content** — `test_render_softkey_separator` verifies only the `fb_hline(39)` separator. The "GO" and "B" labels drawn at `y = 41` are not inspected.
- **Scroll arrow indicators** — `render_menu_list` draws scroll-up and scroll-down arrows (4 pixels each) when the list is partially scrolled. Neither arrow is pixel-tested.
- **`LABEL_CENTER` exact x-coordinate** — `test_render_label_center_sets_pixels` checks that *any* pixel is set in the full-width band, not that the string is horizontally centred. A regression that left-aligns centered labels would pass.
- **Multi-level navigation depth** — `test_nav_back` covers only one GOTO followed by one BACK. Scenarios involving depth 3 or more, or interleaved MENU navigations at different levels, are not tested.
- **Status bar separator line at y = 13** — `render_status_bar` calls `fb_hline(13)`, but no test asserts `row_is_full(13)`.
- **`nokia_keys_raw` polling in the main loop** — the engine's `ui_inject_key` path is fully tested, but the hardware key-scan path (`nokia_keys_raw`) used in the application's `main()` loop is integration-level code and is not exercised by the unit suite.
- **Applet game logic not unit-tested** — `SCREEN_APPLET` init, tick, render, and `on_key` dispatch are untested. A bug in the framework's applet-dispatch path (e.g., calling `tick()` before `init()`, or calling `render()` on a non-top-of-stack applet) would not be caught.
- **Snake collision and scoring not covered** — wall collision, self-collision, food collection, score increment, and speed-step logic in `snake.c` have no assertions. Regressions in game rules are invisible to the test suite.
- **Snake speed system not covered** — `INITIAL_SPEED`, the per-food decrement, and the minimum-speed clamp are not exercised.
- **`ui_fb_*` drawing primitives not covered** — `ui_fb_clear`, `ui_fb_pixel`, `ui_fb_rect`, `ui_fb_hline`, `ui_fb_text`, and `ui_fb_text_inv` have no pixel-level tests. Clipping behaviour and the inversion logic in `ui_fb_text_inv` are untested.
- **`nokia_char_raw` and Messages applet not tested** — the ASCII text-input byte and all associated applet behaviour are not exercised. Specific gaps include:
  - **T9 multi-tap cycling** — pressing the same key repeatedly should cycle through characters; `t9_tap` wrap-around at the end of the character string is untested.
  - **T9 timeout auto-commit** — after 8 ticks without a new tap, `msg_tick()` should call `t9_commit()`; this time-based path is not covered.
  - **T9 uppercase/lowercase toggle** — pressing `*` should commit any pending char and flip `t9_upper`; both the commit-first and the toggle behaviour are untested.
  - **T9 backspace distinction** — with a pending char, `KEY_LEFT` should cancel it without committing; with no pending char and a non-empty buffer, it should remove the last committed char; with an empty buffer, it should exit to `MS_INBOX`. All three branches are untested.
  - **Messages inbox scroll** — `KEY_UP` / `KEY_DOWN` move the cursor and clamp at 0 / `INBOX_COUNT-1`; clamping behaviour is untested.
  - **Messages view state transition** — `KEY_OK` on the inbox should navigate to `MS_VIEW`; any subsequent key should return to `MS_INBOX`. Neither transition is tested.
  - **Compose send flow** — `KEY_RIGHT` in `MS_COMPOSE` should commit any pending char and transition to `MS_SENT`; this path is untested.
  - **`MS_SENT` auto-dismiss** — after 15 ticks in `MS_SENT`, the applet should return to `MS_INBOX` automatically; the tick-countdown is untested.
  - **`nokia_char_raw` cleared on `msg_init()`** — the applet writes `nokia_char_raw = 0` on entry to discard stale input; this invariant is untested.

---

## 9. How to Add a Test

**a. Decide which suite it belongs to**, or declare a new one. Existing suites are `navigation`, `menu_nav`, `call_action`, `validation`, and `rendering`. A new suite requires a `ZTEST_SUITE(my_suite, NULL, NULL, NULL, NULL, NULL)` declaration in the Ztest block.

**b. Write the test function** using `TCHECK` and `TCHECK_EQ`:

```c
static void test_my_feature(void)
{
    INIT_AT(SCR_STATUS);           /* or INIT() for root */
    ui_set_signal(3);
    ui_tick();
    TCHECK(pixel_at(8, 4));        /* boolean: third bar top pixel set at signal=3 */
    TCHECK_EQ(ui_current_screen_id(), SCR_STATUS);
}
```

**c. Register in the Ztest block** (inside `#ifdef CONFIG_ZTEST`):

```c
ZTEST(rendering, test_my_feature) { test_my_feature(); }
```

The first argument must match an existing `ZTEST_SUITE` name, or a new `ZTEST_SUITE` declaration must precede it.

**d. Call it from `main()`** (inside the `#else` standalone block):

```c
test_my_feature();
```

Position the call in the logical section for its suite. The total assertion count in the final summary line will increase automatically.

**e. Verify locally:**

```sh
make -C firmware/tests test
```

Confirm the pass count increases and the summary reads `N/N tests passed` before committing.

---

## 10. `TCHECK` / `TCHECK_EQ` Macro Reference

### `TCHECK(condition)`

Evaluates `condition` once. Succeeds if the result is non-zero.

| Mode | Behaviour on pass | Behaviour on fail |
|---|---|---|
| Standalone | Increments `_pass` | Prints `FAIL file:line expr`, increments `_fail`; **execution continues** |
| Ztest | No output | Calls `zassert_true(condition, #condition)`; **aborts the current test case** |

Use `TCHECK` when the assertion is a boolean expression with no useful "expected vs actual" decomposition, e.g. `TCHECK(pixel_at(x, y))`.

### `TCHECK_EQ(actual, expected)`

Evaluates both arguments, casts them to `int`, and compares for equality.

| Mode | Behaviour on pass | Behaviour on fail |
|---|---|---|
| Standalone | Increments `_pass` | Prints `FAIL file:line expr = actual_value, want expected_value`; **execution continues** |
| Ztest | No output | Calls `zassert_equal((int)a, (int)b, …)`; **aborts the current test case** |

Use `TCHECK_EQ` when comparing integers where the diagnostic message benefits from showing both values, e.g. `TCHECK_EQ(ui_current_menu_cursor(), 3)`.

**Important standalone behaviour:** because `TCHECK` and `TCHECK_EQ` are non-aborting in standalone mode, a test function with multiple assertions may report several failures in a single run, which helps diagnose correlated bugs. In Ztest mode the first failing assertion terminates the test case immediately; subsequent assertions in the same function are not evaluated. Write test functions so that later assertions are still meaningful if earlier ones fail — prefer independent checks over chains where step N depends on step N−1 being true.

---

## 11. Compatibility Matrix

| Mode | Compiler | Board / runner | Build + run command |
|---|---|---|---|
| Standalone host | `cc` (Clang or GCC, any version supporting C99) | Host OS process | `make -C firmware/tests test` |
| Zephyr `native_sim` | `clang` or `gcc` via Zephyr toolchain | POSIX host runner | `west build -b native_sim firmware/tests/ui && west build -t run` |
| Zephyr `native_sim` via Twister | Zephyr toolchain | POSIX host runner | `west twister -T firmware/tests` |
| Zephyr ARM (Cortex-M) | `arm-zephyr-eabi-gcc` | Any ARM board (e.g. `nrf52840dk`) | `west build -b nrf52840dk/nrf52840 firmware/tests/ui` — **builds only, not runnable without hardware** |

The standalone mode has no dependencies beyond a C99 compiler and standard `libc`. It is the recommended first check before every commit. The `native_sim` mode provides Ztest's structured reporting, per-suite isolation, and compatibility with Twister's CI integration, but requires a Zephyr workspace (`west init` / `west update`) and the Zephyr SDK.

---

## 12. Applet System Test Scenarios

The following test cases are **specified but not yet implemented** as code. They document the intended behaviour of the applet dispatch path and serve as a guide for a future `applet` test suite. All scenarios assume a test fixture that registers a mock `applet_t` whose callbacks set observable flags.

### Fixture

```c
static int mock_init_count   = 0;
static int mock_tick_count   = 0;
static int mock_render_count = 0;
static nokia_key_t mock_last_key = KEY_NONE;

static void mock_init(void)            { mock_init_count++; }
static void mock_tick(void)            { mock_tick_count++; }
static void mock_render(void)          { mock_render_count++; }
static void mock_on_key(nokia_key_t k) { mock_last_key = k; }

applet_t mock_applet = {
    .init   = mock_init,
    .tick   = mock_tick,
    .render = mock_render,
    .on_key = mock_on_key,
};
```

`SCR_APPLET_TEST` is added to the test screen table using `APPLET_SCREEN("MockApplet", &mock_applet)`.

### Test cases

| # | Test name | Setup | Action | Expected outcome |
|---|---|---|---|---|
| A1 | `test_applet_goto_calls_init` | `INIT_AT(SCR_HOME)` | `ui_inject_key(KEY_LEFT)` (binds `GOTO(SCR_APPLET_TEST)`) | `mock_init_count == 1` |
| A2 | `test_applet_tick_and_render_called` | `INIT_AT(SCR_APPLET_TEST)` | `ui_tick()` | `mock_tick_count == 1 && mock_render_count == 1` |
| A3 | `test_applet_tick_not_called_when_not_top` | `INIT_AT(SCR_APPLET_TEST)`, push a second screen | `ui_tick()` | `mock_tick_count == 0` (applet is not top-of-stack) |
| A4 | `test_applet_on_key_routed` | `INIT_AT(SCR_APPLET_TEST)` | `ui_inject_key(KEY_UP)` | `mock_last_key == KEY_UP` |
| A5 | `test_applet_ui_back_returns_to_previous` | `INIT_AT(SCR_HOME)`, navigate to `SCR_APPLET_TEST` | Call `ui_back()` from inside `mock_on_key` when `KEY_RIGHT` | `ui_current_screen_id() == SCR_HOME` after the key event |
| A6 | `test_applet_init_called_once_per_push` | `INIT_AT(SCR_HOME)` | `GOTO(SCR_APPLET_TEST)`, `ui_back()`, `GOTO(SCR_APPLET_TEST)` | `mock_init_count == 2` (fresh init on second push) |
| A7 | `test_static_screen_unaffected_below_applet` | `INIT_AT(SCR_HOME)`, push `SCR_APPLET_TEST` | `ui_tick()` (applet is top-of-stack) | `nokia_fb` does **not** contain pixels from `SCR_HOME`'s `HLINE(25)` widget — the widget loop for the lower screen is not executed |
| A8 | `test_applet_key_not_routed_to_screen_action` | Screen def for `SCR_APPLET_TEST` has `on_left = GOTO(SCR_HOME)` (hypothetical) | `ui_inject_key(KEY_LEFT)` while applet is top | `ui_current_screen_id() == SCR_APPLET_TEST` (applet's `on_key` is called instead of the screen-level action) |

**Implementation note:** Tests A1–A8 require no changes to `ui_framework.c` to become runnable; they only require adding `SCR_APPLET_TEST` to the test fixture in `test_main.c` and implementing the mock applet. The applet dispatch logic is already present in the framework.

---

## 13. Messages Applet and T9 Test Scenarios

The following test cases are **specified but not yet implemented**. They document the intended behaviour of the Messages applet (`applet_messages.c`) and the T9 multi-tap input system. All scenarios assume the test builds include `applet_messages.c` and link `nokia_char_raw` as a writable global.

### Fixture

```c
/* Expose nokia_char_raw for direct injection in tests */
extern volatile uint8_t nokia_char_raw;

/* Helper: push SCR_MESSAGES onto the nav stack */
#define INIT_MESSAGES() do {          \
    ui_init(g_screens, SCR_COUNT, SCR_MAIN_MENU); \
    ui_inject_key(KEY_LEFT); /* GOTO(SCR_MESSAGES) via menu item */ \
} while (0)

/* Helper: pump N ticks through the applet */
static void tick_n(int n) { for (int i = 0; i < n; i++) ui_tick(); }

/* Helper: inject a char key */
static void inject_char(char ch) { nokia_char_raw = (uint8_t)ch; ui_tick(); }
```

### Test Cases

| # | Test name | Setup | Action | Expected outcome |
|---|-----------|-------|--------|-----------------|
| M1 | `test_msg_inbox_cursor_down` | `INIT_MESSAGES()` — starts in `MS_INBOX`, cursor=0 | `ui_inject_key(KEY_DOWN)` | Internal inbox cursor advances to 1 (verified via render: row 1 is inverted, row 0 is not) |
| M2 | `test_msg_inbox_cursor_clamps_at_bottom` | `INIT_MESSAGES()`, send 10 × `KEY_DOWN` | — | Cursor clamped at 2 (`INBOX_COUNT-1`); no out-of-bounds access |
| M3 | `test_msg_inbox_ok_enters_view` | `INIT_MESSAGES()` | `ui_inject_key(KEY_OK)` | State transitions to `MS_VIEW`; `ui_tick()` renders sender name "Alice" in title area (pixels set in y=1..8 region) |
| M4 | `test_msg_view_any_key_returns_inbox` | `INIT_MESSAGES()`, then `KEY_OK` to enter `MS_VIEW` | `ui_inject_key(KEY_DOWN)` | State returns to `MS_INBOX`; `ui_tick()` renders "Messages" title |
| M5 | `test_msg_compose_t9_single_tap` | `INIT_MESSAGES()`, then `KEY_LEFT` to enter `MS_COMPOSE` | `inject_char('2')` | Pending char is `'A'` (uppercase default); rendered inverted at cursor; `compose_buf` is still empty (not yet committed) |
| M6 | `test_msg_compose_t9_multi_tap_cycles` | `INIT_MESSAGES()`, enter `MS_COMPOSE` | `inject_char('2')` three times in quick succession | After tap 1: pending = `'A'`; after tap 2: pending = `'B'`; after tap 3: pending = `'C'` |
| M7 | `test_msg_compose_t9_timeout_commits` | `INIT_MESSAGES()`, enter `MS_COMPOSE`, `inject_char('2')` | `tick_n(8)` (T9_TIMEOUT ticks without another tap) | `compose_buf[0] == 'A'`; `t9_key` reset to −1; cursor advances; blinking underscore visible |
| M8 | `test_msg_compose_t9_uppercase_toggle` | `INIT_MESSAGES()`, enter `MS_COMPOSE` | `inject_char('*')` | `t9_upper` flips to `false`; title renders `"Write Msg [a]"`; subsequent `inject_char('2')` yields pending char `'a'` |
| M9 | `test_msg_compose_del_cancels_pending` | `INIT_MESSAGES()`, enter `MS_COMPOSE`, `inject_char('3')` (pending = `'D'`) | `ui_inject_key(KEY_LEFT)` | Pending char cancelled (`t9_key == -1`); `compose_buf` unchanged (empty); state remains `MS_COMPOSE` |
| M10 | `test_msg_compose_del_removes_committed` | `INIT_MESSAGES()`, enter `MS_COMPOSE`, `inject_char('2')`, `tick_n(8)` (commits `'A'`) | `ui_inject_key(KEY_LEFT)` | `compose_buf` is now empty; `compose_len == 0`; state remains `MS_COMPOSE` |
| M11 | `test_msg_compose_del_exits_on_empty` | `INIT_MESSAGES()`, enter `MS_COMPOSE` (buffer empty) | `ui_inject_key(KEY_LEFT)` | State transitions back to `MS_INBOX` |
| M12 | `test_msg_compose_send_transitions_sent` | `INIT_MESSAGES()`, enter `MS_COMPOSE` | `ui_inject_key(KEY_RIGHT)` | State transitions to `MS_SENT`; `ui_tick()` renders "Message sent!" banner (pixels set in y=18..28 region) |
| M13 | `test_msg_sent_auto_dismiss` | `INIT_MESSAGES()`, enter `MS_COMPOSE`, `KEY_RIGHT` → `MS_SENT` | `tick_n(15)` | After 15 ticks, state returns to `MS_INBOX` automatically |
| M14 | `test_msg_init_clears_nokia_char_raw` | Set `nokia_char_raw = '5'` before navigating to `MS_MESSAGES` | Navigate to `SCR_MESSAGES` (calls `msg_init()`) | `nokia_char_raw == 0` immediately after `msg_init()` runs; stale keypress not delivered to compose |

**Implementation note:** Tests M1–M14 require:
1. `applet_messages.c` and `applet_messages.h` compiled into the test binary.
2. `nokia_char_raw` accessible as a writable global (already `extern volatile uint8_t` in `main.c`).
3. A way to inspect internal applet state or infer it from `nokia_fb` pixels. For state transitions (M3, M4, M11, M12, M13) the render output is the observable: check for known text or pixel patterns in the rendered frame. For T9 state (M5, M6, M7, M8) expose a test accessor or verify via pixel rendering.

