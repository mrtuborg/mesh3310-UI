# Nokia 3310 UI Framework — Architecture Decision Records

This document captures the key architectural decisions made during the design of the Nokia 3310 UI Framework. Each record follows the standard ADR format: **Title**, **Status**, **Context**, **Decision**, **Consequences**.

---

## ADR-001: Declarative UI Model

**Status:** Accepted

### Context

The firmware runs on an nRF52840 inside Renode with very limited RAM (~256 KB). The UI needs to be easy to extend by adding screens without modifying the engine. The alternatives considered were:

- A retained-mode widget tree (widgets are heap-allocated objects, mutations tracked)
- An immediate-mode API (app code calls draw functions every frame directly)
- A declarative `static const` data-structure model (screens described as read-only tables)

### Decision

All screens, widgets, and key-action bindings are described as `static const` C data structures in `screens.c`. The engine in `ui_framework.c` iterates over these tables at render time and executes actions on key events. No heap allocation is used. No per-screen logic lives in the engine.

### Consequences

- **Positive:** Zero heap fragmentation; all UI data lives in flash. Adding a screen requires only editing `screens.c`. The engine is testable in isolation from screen content. The entire UI can be described and reviewed without reading any function bodies.
- **Positive:** The `static const` constraint is enforced by the compiler; application developers cannot accidentally mutate screen definitions at runtime.
- **Negative:** Dynamic or data-driven UIs (e.g., a contacts list populated from BLE) require the `SCREEN_APPLET` escape hatch (see ADR-002) or a dedicated dynamic widget type.
- **Negative:** `CALL(fn)` requires a file-scope function pointer constant; closures and lambda-style callbacks are not expressible in C99.

---

## ADR-002: Applet Interface Pattern

**Status:** Accepted

### Context

Some screens require stateful, frame-by-frame logic that cannot be expressed as a static widget array — for example, a Snake game, a real-time clock face, or an animated progress indicator. Options considered:

- Extend `screen_def_t` with optional `tick` and `render` function pointers (partial subclassing)
- Introduce a full subclassing mechanism by embedding a `vtable` pointer in `screen_def_t`
- A separate `applet_t` struct with exactly four callbacks (`init`, `tick`, `render`, `on_key`), referenced from `screen_def_t` via a pointer

### Decision

A separate `applet_t` struct with four callbacks is used. `screen_def_t` gains a `type = SCREEN_APPLET` variant and an `applet` pointer field. The engine calls the four callbacks at the appropriate lifecycle points. Application code declares a `screen_def_t` with `APPLET_SCREEN(name, ptr)`.

### Consequences

- **Positive:** The four-callback interface is minimal and explicit. There is no ambiguity about when each callback fires.
- **Positive:** Applets are self-contained; `init()` resets all state, so the applet is trivially re-entrant (push, pop, push again calls `init()` a second time).
- **Positive:** The declarative screens model is preserved: all `SCREEN_STATIC` and `SCREEN_MENU` screens remain pure data. The `SCREEN_APPLET` variant is an opt-in escape hatch.
- **Negative:** `SCREEN_APPLET` screens cannot use the widget catalog; they must implement their own rendering via `ui_fb_*`. This is intentional — applets by definition require custom rendering.
- **Negative:** The `cursor` and `scroll` fields in `nav_entry_t` are unused for applet screens, wasting 8 bytes per stack entry.

---

## ADR-003: Framebuffer Layout

**Status:** Accepted

### Context

The Nokia 3310 / PCD8544 LCD controller uses a page-based memory model: the display is divided into 6 horizontal pages of 8 rows each, and each byte in the controller's RAM encodes one vertical column of 8 pixels within a page (LSB = top pixel). Options considered:

- **Flat bitmap:** `uint8_t nokia_fb[48][84]` — one byte per pixel row per column, with pixel packing done at render time
- **PCD8544 native layout:** `uint8_t nokia_fb[6][84]` — matches the controller's page layout exactly

### Decision

`nokia_fb` uses the PCD8544 native page layout: `nokia_fb[page][column]`, where each byte encodes 8 vertically stacked pixels. Setting pixel `(x, y)` is `nokia_fb[y/8][x] |= (1 << (y%8))`.

### Consequences

- **Positive:** The framebuffer can be copied to the physical LCD without any format conversion; bit manipulation is the same in firmware and on the real hardware.
- **Positive:** Total framebuffer size is 504 bytes — small enough that `lcd_viewer.py` can read it in a single `sysbus ReadBytes` call.
- **Positive:** `fb_hline(y)` can be implemented as a loop over 84 columns rather than a bit-mask operation, keeping the implementation simple.
- **Negative:** Pixel coordinate access requires a division and a shift (`y/8`, `y%8`); a flat bitmap would use a single array index. On Cortex-M4 this is negligible.
- **Negative:** Horizontal fill operations spanning page boundaries require writes to two pages; `fb_fill_rect` must handle the split.

---

## ADR-004: Renode Telnet Polling

**Status:** Accepted

### Context

`lcd_viewer.py` must read 504 bytes from `nokia_fb` in Renode's simulated address space each frame (~10 Hz). Communication options considered:

- **Persistent TCP connection** to Renode's GDB stub or a custom UART peripheral
- **Memory-mapped GPIO / semihosting** triggered by the firmware writing a sentinel value
- **Poll-per-frame via `sysbus ReadBytes`** over the Renode monitor telnet port (`:1234`), with the client reconnecting each frame

### Decision

`lcd_viewer.py` opens a new telnet connection to the Renode monitor port for each frame, sends `sysbus ReadBytes <address> 504`, reads the response, and closes the connection. A fresh connection is established on every iteration of the viewer loop.

### Consequences

- **Positive:** The Renode monitor port remains free for manual `telnet localhost 1234` sessions between frames; a human operator can inject keys or inspect memory at any time without interfering with the viewer (the viewer's connection is closed before the next human command arrives).
- **Positive:** No persistent daemon is required; `lcd_viewer.py` is a standalone script with no background thread managing a connection.
- **Positive:** If Renode restarts or the simulation is reset, the viewer automatically reconnects on the next frame without special error recovery.
- **Negative:** Each frame incurs TCP connection-setup overhead (~1 ms on loopback). At 10 Hz this is negligible, but it would become a bottleneck at frame rates above ~100 Hz.
- **Negative:** If a manual telnet session holds the monitor port open, the viewer cannot connect and will skip frames until the session is closed.

---

## ADR-005: matplotlib macOS Backend for LCD Viewer

**Status:** Accepted

### Context

`lcd_viewer.py` needs a GUI window to display the 84×48 LCD image, upscaled for readability. The original implementation used `tkinter` with `PIL.ImageTk`. This broke when the development environment used system Python 3.9 (macOS Ventura), which ships Tk 8.5. PIL 11.3.0's `ImageTk` module requires Tk ≥ 8.6 and raises a runtime error (`_tkinter.TclError: Can't find a usable init.tcl`) on Tk 8.5.

Options considered:

- **Upgrade Tk to 8.6** via Homebrew — requires a Homebrew Python or a manual Tcl/Tk install; not portable across developer machines
- **Downgrade PIL** to a version compatible with Tk 8.5 — would lose other PIL improvements and create a version pin
- **Replace tkinter with matplotlib** using the `macosx` native backend — ships with every `pip install matplotlib`; no Tk dependency

### Decision

`lcd_viewer.py` uses `matplotlib` with the `macosx` backend (`matplotlib.use("macosx")`). The image is rendered via `imshow` on a `Figure` with pixel-accurate sizing. The `animation.FuncAnimation` loop drives the 10 Hz refresh. The `tkinter` / `PIL.ImageTk` dependency is removed entirely.

### Consequences

- **Positive:** Eliminates the Tk version incompatibility; works with system Python 3.9+ on macOS without any additional package manager intervention.
- **Positive:** `matplotlib` provides zoom/pan toolbar for free, useful for inspecting pixel-level rendering.
- **Negative:** The `macosx` backend requires a native macOS GUI event loop and does not work headlessly or on Linux without switching to a different backend (e.g., `TkAgg` or `Qt5Agg`). Linux developers must set `MPLBACKEND` or edit the backend selection.
- **Negative:** `matplotlib` is a heavier dependency than `tkinter` (which is part of the Python standard library). A fresh environment requires `pip install matplotlib`.

**Update (two-window redesign):** `lcd_viewer.py` was subsequently redesigned from a single window to **two separate matplotlib figures**. Window 1 (`"Nokia 3310 — Screen"`) contains only the 84×48 LCD image centred inside a dark bezel; its size is locked to `(LCD_WIDTH×S + 2×BEZEL) × (LCD_HEIGHT×S + 2×BEZEL)` pixels. Window 2 (`"Nokia 3310 — Keys"`) contains the Nokia 3310 navigation buttons and the T9 numeric keypad, with width capped at `LCD_WIDTH × min(S, 5)`. Both windows lock their sizes via `resize_event` handlers. The choice of `matplotlib` and the `macosx` backend was preserved unchanged; the redesign required no change to the underlying backend decision documented in this ADR.

---

## ADR-006: `nokia_char_raw` for Text Input

**Status:** Accepted

### Context

The keypad input path (`nokia_keys_raw`) encodes physical key events as bitmask values (`KEY_UP`, `KEY_DOWN`, etc.). It carries no semantic information about text characters. A future SMS/text-entry applet needs to deliver ASCII character codes produced by multi-tap T9 logic (running on the host side or in a Renode peripheral model) to the firmware. Options considered:

- **Extend `nokia_keys_raw`** with a second byte in the same word — complicates the existing key-bitmask API
- **Encode text events as special `KEY_*` values** above the existing bitmask range — fragile; collides with future physical key additions
- **A separate `nokia_char_raw` byte** at a distinct address — clean separation of concerns; consumed independently of key events

### Decision

A second `volatile uint8_t nokia_char_raw` is declared `__attribute__((used))` at file scope in `main.c`. The main loop reads it alongside `nokia_keys_raw`; if non-zero, clears it and delivers the ASCII code to the active applet's `on_key` path (or to a future SMS handler). `nokia_char_raw` is independent of `nokia_keys_raw`; both may be non-zero in the same tick.

### Consequences

- **Positive:** Clean separation between physical key events and logical text characters. Existing key-routing code is unaffected.
- **Positive:** The SMS handler can be added later without changing the key-injection API.
- **Negative:** Requires consumers (applets, SMS handler) to poll two variables per tick. A missed `nokia_char_raw` value (e.g., because the applet does not handle it) is silently dropped, same as `nokia_keys_raw`.
- **Positive:** The Messages applet (`applet_messages.c`) is now the active consumer. `msg_tick()` reads and clears `nokia_char_raw` each tick; `msg_init()` also writes `nokia_char_raw = 0` on applet entry to discard any stale keypress injected while a different screen was on top of the navigation stack. This init-time clear is an **invariant that every future consumer must also implement** to avoid processing stale input on applet entry.

---

## ADR-007: Messages Screen Implemented as an Applet

**Status:** Accepted

### Context

The `SCR_MESSAGES` screen requires stateful, frame-by-frame behaviour that cannot be expressed with the declarative `SCREEN_STATIC` or `SCREEN_MENU` types:

- An inbox with a highlighted cursor that moves with key presses
- A compose mode with per-tick T9 timers (`t9_timer`, `blink_ticks`, `sent_ticks`)
- Auto-dismiss logic (`MS_SENT` → `MS_INBOX` after 15 ticks)
- Direct access to `nokia_char_raw` to consume T9 input

Options considered:

- **Static widget array** — `SCREEN_STATIC` with `CALL()` actions for each button: cannot express per-tick timer logic, cursor highlighting, or multi-state rendering without extending the widget catalog
- **New `SCREEN_DYNAMIC` type** in the engine — would require engine changes and a new widget dispatch path for every dynamic screen, with no clear stopping point
- **`SCREEN_APPLET`** — the existing escape hatch (ADR-002) already provides exactly the `init/tick/render/on_key` lifecycle that the messages screen needs

### Decision

`SCR_MESSAGES` is implemented as `APPLET_SCREEN("Messages", &messages_applet)`. All inbox state, T9 state, and compose buffer are owned by `applet_messages.c` as `static` module-level variables. The engine sees only the four-callback interface.

### Consequences

- **Positive:** No engine changes required. `applet_messages.c` is fully self-contained and isolated from the rest of the UI.
- **Positive:** The four-callback interface provides the correct lifecycle: `init()` resets all state on entry, `tick()` advances timers and consumes `nokia_char_raw`, `render()` draws the frame, `on_key()` handles navigation transitions.
- **Positive:** Adding a new state (e.g., a contact picker, a message thread view) only requires adding a new `msg_state_t` value and new `case` branches in `msg_tick`, `msg_render`, and `msg_on_key`.
- **Negative:** `applet_messages.c` cannot use the widget catalog (`STATUS_BAR`, `SOFTKEY_BAR`, etc.). All drawing is done via `ui_fb_*` primitives, duplicating some layout constants from `ui_framework.c` (e.g., `SK_HLINE_Y = 39`, `SK_TEXT_Y = 41`).
- **Negative:** `SCREEN_APPLET` screens have no host-test coverage at present (see test-spec.md §8). The Messages applet logic is exercised only through integration testing (running in Renode with `lcd_viewer.py`).

---

## ADR-008: T9 Multi-Tap Logic Implemented in Firmware

**Status:** Accepted

### Context

T9 multi-tap requires per-tick state: a pending key index, a tap count, and a timeout counter that decrements each 100 ms tick. This state must live somewhere. Two architectures were considered:

- **Host-side T9 (in `lcd_viewer.py`)**: The viewer tracks multi-tap state and writes the final decoded letter to `nokia_char_raw` only after the timeout. `nokia_char_raw` would carry the finished letter (`'A'`, `'B'`, …), not the raw digit.
- **Firmware-side T9 (in `applet_messages.c`)**: The viewer writes the raw digit ASCII (`'2'`, `'3'`, …) to `nokia_char_raw` on every keypress. The firmware tracks tap count and timeout, and decides which character to display or commit.

### Decision

T9 state is kept entirely in firmware (`applet_messages.c`). `nokia_char_raw` carries the raw digit ASCII (e.g., `'2'` for the ABC key, `'*'`, `'#'`), not the decoded letter. The applet's `msg_tick()` advances `t9_timer` every tick and calls `t9_commit()` when `t9_timer >= T9_TIMEOUT`.

### Consequences

- **Positive — separation of concerns:** `lcd_viewer.py` does not need to know anything about which applet is active or what T9 characters it expects. It simply writes the pressed key's ASCII value and is done. A future applet that uses the keypad differently (e.g., a calculator) requires no changes to the viewer.
- **Positive — natural tick rate:** The Zephyr main loop already sleeps 100 ms per iteration. `T9_TIMEOUT = 8` ticks gives a 800 ms commit window with no extra timer infrastructure.
- **Positive — consistent pending-char rendering:** The firmware knows the current pending character at every tick, so it can render the inverted pending-char cell every frame without any round-trip to the host.
- **Negative:** `lcd_viewer.py` sends a raw digit on every physical key event. If the user holds down a key on the host keyboard and the OS auto-repeats, multiple events arrive, each advancing `t9_tap`. This is intentional (Nokia hardware worked the same way — repeated physical presses cycle through characters) but may surprise users expecting hold-to-type behaviour.
- **Negative:** The T9 character map is duplicated: `KEYPAD_LAYOUT` in `lcd_viewer.py` (for display only) and `T9_CHARS` in `applet_messages.c` (for decode). A change to the character set must be made in both places.

