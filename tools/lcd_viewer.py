#!/usr/bin/env python3
import argparse
import socket
import time

import matplotlib
matplotlib.use("macosx")          # native macOS Quartz backend — no Tk required
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from matplotlib.patches import FancyBboxPatch
import numpy as np

LCD_WIDTH  = 84
LCD_HEIGHT = 48
LCD_BYTES  = LCD_WIDTH * LCD_HEIGHT // 8  # 504 bytes

# Nokia key bitmasks (must match ui_framework.h)
KEY_LEFT  = 0x01
KEY_RIGHT = 0x02
KEY_UP    = 0x04
KEY_DOWN  = 0x08
KEY_OK    = 0x10

# ------------------------------------------------------------------ #
# Nokia T9 keypad layout                                             #
# ------------------------------------------------------------------ #
KEYPAD_LAYOUT = [
    [('1', '.,!?'), ('2', 'ABC'),  ('3', 'DEF') ],
    [('4', 'GHI'),  ('5', 'JKL'),  ('6', 'MNO') ],
    [('7', 'PQRS'), ('8', 'TUV'),  ('9', 'WXYZ')],
    [('*', ''),     ('0', '+ '),   ('#', '')     ],
]

# ------------------------------------------------------------------ #
# Nokia 3310 navigation layout (accurate to original 2000 phone)     #
#                                                                     #
#  [ ◄ Back ]   [ ▲ ]   [ Menu ► ]                                   #
#               [ ● ]                                                 #
#               [ ▼ ]                                                 #
#                                                                     #
#  Left SK  = KEY_LEFT  (Back / Cancel)                               #
#  Right SK = KEY_RIGHT (Names / Menu)                                #
#  ▲        = KEY_UP                                                  #
#  ▼        = KEY_DOWN                                                #
#  ●        = KEY_OK    (Select / Enter)                              #
# ------------------------------------------------------------------ #
# Axes: xlim=[0,7], ylim=[0,4]
# Each entry: name -> (cx, cy, w, h, label, key_mask)
NAV_BUTTONS = {
    "back":  (1.0,  2.5,  1.6, 0.80, "◄  Back",  KEY_LEFT),
    "up":    (3.5,  3.2,  0.9, 0.70, "▲",         KEY_UP),
    "ok":    (3.5,  2.0,  1.0, 0.90, "●",         KEY_OK),
    "down":  (3.5,  0.8,  0.9, 0.70, "▼",         KEY_DOWN),
    "menu":  (6.0,  2.5,  1.6, 0.80, "Menu  ►",  KEY_RIGHT),
}

# Host keyboard → nokia_keys_raw bitmask
KEY_MAP = {
    "left":   KEY_LEFT,
    "right":  KEY_RIGHT,
    "up":     KEY_UP,
    "down":   KEY_DOWN,
    "return": KEY_OK,
    " ":      KEY_OK,
}

# Host keyboard key name → nav button name (for visual flash)
KEYBOARD_TO_NAV = {
    "left": "back", "right": "menu",
    "up":   "up",   "down":  "down",
    "return": "ok", " ": "ok",
}

# Host keyboard → ASCII char code
CHAR_KEY_MAP = {ch: ord(ch) for ch in '1234567890'}
CHAR_KEY_MAP.update({'*': ord('*'), '#': ord('#'),
                     'asterisk': ord('*'), 'numbersign': ord('#')})


class RenodeClient:
    def __init__(self, host, port):
        self.host = host
        self.port = port

    def _connect(self):
        return socket.create_connection((self.host, self.port), timeout=1.0)

    def read_mem(self, addr, size):
        # Connect fresh each call so the monitor port is free between frames
        try:
            sock = self._connect()
        except OSError:
            return [0x00] * size

        sock.settimeout(0.5)
        try:
            # Discard the Telnet banner (IAC negotiation bytes sent on connect)
            try:
                sock.recv(64)
            except socket.timeout:
                pass

            # Send the read command
            sock.settimeout(2.0)
            cmd = f"sysbus ReadBytes 0x{addr:x} {size}\n"
            sock.sendall(cmd.encode("ascii"))

            # Read until the closing ] of the byte array
            data = b""
            deadline = time.time() + 2.0
            while time.time() < deadline:
                try:
                    chunk = sock.recv(8192)
                    if not chunk:
                        break
                    data += chunk
                    if b"]" in data:
                        break
                except socket.timeout:
                    break
        finally:
            sock.close()

        # Parse only the content between [ and ]
        out = []
        text = data.decode(errors="ignore")
        start = text.find("[")
        end   = text.find("]", start) if start >= 0 else -1
        if start >= 0 and end >= 0:
            for tok in text[start+1:end].replace(",", " ").split():
                if tok.startswith("0x"):
                    try:
                        val = int(tok, 16)
                        if val <= 0xFF:
                            out.append(val)
                    except ValueError:
                        pass

        if len(out) < size:
            out.extend([0x00] * (size - len(out)))

        return out[:size]

    def write_byte(self, addr, value):
        """Inject a single byte into the simulator (used for key presses)."""
        try:
            sock = self._connect()
        except OSError:
            return
        sock.settimeout(0.5)
        try:
            try:
                sock.recv(64)   # discard Telnet banner
            except socket.timeout:
                pass
            sock.settimeout(1.0)
            cmd = f"sysbus WriteByte 0x{addr:x} 0x{value:02x}\n"
            sock.sendall(cmd.encode("ascii"))
            try:
                sock.recv(256)  # wait for prompt so command completes
            except socket.timeout:
                pass
        finally:
            sock.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--address",      required=True,  type=lambda x: int(x, 0),
                    help="nokia_fb RAM address")
    ap.add_argument("--keys-address", required=False, type=lambda x: int(x, 0),
                    default=None, help="nokia_keys_raw address (nav keys)")
    ap.add_argument("--char-address", required=False, type=lambda x: int(x, 0),
                    default=None, help="nokia_char_raw address (text keypad)")
    ap.add_argument("--host",  default="127.0.0.1")
    ap.add_argument("--port",  default=1234, type=int)
    ap.add_argument("--scale", default=4,    type=int)
    ap.add_argument("--fps",   default=10,   type=int)
    args = ap.parse_args()

    renode    = RenodeClient(args.host, args.port)
    fb_addr   = args.address
    keys_addr = args.keys_address
    char_addr = args.char_address

    S  = args.scale
    FG = np.array([27,  47,  27],  dtype=np.uint8)
    BG = np.array([155, 207, 155], dtype=np.uint8)

    dpi = 100
    matplotlib.rcParams["toolbar"] = "None"

    # ================================================================ #
    # Window 1 — LCD screen                                            #
    #   Exact pixel size: LCD_WIDTH×S  by  LCD_HEIGHT×S               #
    #   Dark bezel border around the green display                     #
    # ================================================================ #
    BEZEL = max(6, S)          # dark border around the LCD in pixels
    lcd_w_px = LCD_WIDTH  * S
    lcd_h_px = LCD_HEIGHT * S
    win1_w   = lcd_w_px + BEZEL * 2
    win1_h   = lcd_h_px + BEZEL * 2

    fig_lcd = plt.figure("Nokia 3310 — Screen",
                         figsize=(win1_w / dpi, win1_h / dpi), dpi=dpi)
    fig_lcd.patch.set_facecolor("#1a1a1a")

    # LCD display centred inside the bezel
    ax_lcd = fig_lcd.add_axes([
        BEZEL / win1_w,
        BEZEL / win1_h,
        lcd_w_px / win1_w,
        lcd_h_px / win1_h,
    ])
    ax_lcd.axis("off")

    def _lock_lcd(_evt):
        fig_lcd.set_size_inches(win1_w / dpi, win1_h / dpi, forward=False)
    fig_lcd.canvas.mpl_connect("resize_event", _lock_lcd)
    fig_lcd.set_size_inches(win1_w / dpi, win1_h / dpi, forward=True)

    # ================================================================ #
    # Window 2 — Controls (Nokia-style nav + T9 keypad)                #
    #   Fixed width matching the LCD window.                           #
    #   Nav area: Nokia 3310 accurate button layout.                   #
    #   T9 area:  3×4 numeric keypad.                                  #
    # ================================================================ #
    # Use a capped scale for the control panel so it stays screen-friendly
    CS       = min(S, 5)
    ctrl_w   = LCD_WIDTH * CS + BEZEL * 2     # match LCD window width feel
    ctrl_pad = max(10, CS * 2)
    nav_h    = max(150, CS * 30)              # Nokia nav buttons section
    t9_h     = max(220, CS * 44)             # T9 keypad section
    ctrl_h   = ctrl_pad + nav_h + ctrl_pad // 2 + t9_h + ctrl_pad

    fig_ctrl = plt.figure("Nokia 3310 — Keys",
                          figsize=(ctrl_w / dpi, ctrl_h / dpi), dpi=dpi)
    fig_ctrl.patch.set_facecolor("#1a1a1a")

    def _lock_ctrl(_evt):
        fig_ctrl.set_size_inches(ctrl_w / dpi, ctrl_h / dpi, forward=False)
    fig_ctrl.canvas.mpl_connect("resize_event", _lock_ctrl)
    fig_ctrl.set_size_inches(ctrl_w / dpi, ctrl_h / dpi, forward=True)

    MX = ctrl_pad / ctrl_w   # horizontal margin fraction

    def _ctrl_rect(bot_px, h_px):
        return [MX, bot_px / ctrl_h, 1 - 2 * MX, h_px / ctrl_h]

    t9_bot  = ctrl_pad
    nav_bot = ctrl_pad + t9_h + ctrl_pad // 2

    # ---------------------------------------------------------------- #
    # Nav axes — Nokia 3310 button layout                              #
    #   xlim=[0,7]  ylim=[0,4]                                         #
    #   [ ◄ Back ]   [ ▲ ]   [ Menu ► ]                               #
    #                [ ● ]                                             #
    #                [ ▼ ]                                             #
    # ---------------------------------------------------------------- #
    ax_nav = fig_ctrl.add_axes(_ctrl_rect(nav_bot, nav_h))
    ax_nav.set_facecolor("#1e1e1e")
    ax_nav.set_xlim(0, 7)
    ax_nav.set_ylim(0, 4)
    ax_nav.axis("off")

    NAV_COLOR = "#3a3a3a" if keys_addr else "#262626"
    NAV_PRESS = "#5a9e5a"
    NAV_EDGE  = "#606060"

    nav_patches = {}   # name -> FancyBboxPatch

    for name, (cx, cy, bw, bh, label, _mask) in NAV_BUTTONS.items():
        patch = FancyBboxPatch(
            (cx - bw / 2, cy - bh / 2), bw, bh,
            boxstyle="round,pad=0.06",
            facecolor=NAV_COLOR,
            edgecolor=NAV_EDGE,
            linewidth=0.9,
            zorder=2,
        )
        ax_nav.add_patch(patch)
        nav_patches[name] = patch

        fs = 11 if name == "ok" else (8 if name in ("back", "menu") else 12)
        ax_nav.text(cx, cy, label,
                    ha="center", va="center",
                    color="#e0e0e0" if keys_addr else "#484848",
                    fontsize=fs, fontweight="bold", zorder=3)

    if not keys_addr:
        ax_nav.text(3.5, 0.2, "Pass --keys-address to enable nav keys",
                    ha="center", va="center", color="#484848", fontsize=5.5)

    # ---------------------------------------------------------------- #
    # T9 axes  —  xlim=[0,3]  ylim=[0,4]                              #
    # ---------------------------------------------------------------- #
    ax_keys = fig_ctrl.add_axes(_ctrl_rect(t9_bot, t9_h))
    ax_keys.set_facecolor("#212121")
    ax_keys.set_xlim(0, 3)
    ax_keys.set_ylim(0, 4)
    ax_keys.axis("off")

    T9_ROWS  = len(KEYPAD_LAYOUT)
    T9_BW    = 0.84
    T9_BH    = 0.76
    T9_BX    = (1 - T9_BW) / 2
    T9_BY    = (1 - T9_BH) / 2
    T9_COLOR = "#3a3a3a" if char_addr else "#262626"
    T9_PRESS = "#5a9e5a"

    t9_patches = {}

    for row, key_row in enumerate(KEYPAD_LAYOUT):
        y_base = T9_ROWS - 1 - row
        for col, (main_ch, letters) in enumerate(key_row):
            patch = FancyBboxPatch(
                (col + T9_BX, y_base + T9_BY), T9_BW, T9_BH,
                boxstyle="round,pad=0.03",
                facecolor=T9_COLOR,
                edgecolor="#505050",
                linewidth=0.8,
                zorder=2,
            )
            ax_keys.add_patch(patch)
            t9_patches[(row, col)] = patch

            ax_keys.text(col + 0.5, y_base + 0.70, main_ch,
                         ha="center", va="center",
                         color="#e0e0e0" if char_addr else "#484848",
                         fontsize=10, fontweight="bold",
                         fontfamily="monospace", zorder=3)
            if letters:
                ax_keys.text(col + 0.5, y_base + 0.28, letters,
                             ha="center", va="center",
                             color="#888888" if char_addr else "#3a3a3a",
                             fontsize=5, fontfamily="monospace", zorder=3)

    if not char_addr:
        ax_keys.text(1.5, -0.14, "Pass --char-address to enable text keypad",
                     ha="center", va="center", color="#484848", fontsize=5.5)

    # ---------------------------------------------------------------- #
    # Flash helpers (draw_idle on the controls figure)                 #
    # ---------------------------------------------------------------- #
    _nav_hl = {"name": None, "ts": 0.0}
    _t9_hl  = {"pos":  None, "ts": 0.0}

    def flash_nav(name):
        if _nav_hl["name"]:
            p = nav_patches.get(_nav_hl["name"])
            if p: p.set_facecolor(NAV_COLOR)
        p = nav_patches.get(name)
        if p: p.set_facecolor(NAV_PRESS)
        _nav_hl["name"] = name
        _nav_hl["ts"]   = time.time()
        fig_ctrl.canvas.draw_idle()

    def flash_t9(row, col):
        if _t9_hl["pos"]:
            r, c = _t9_hl["pos"]
            p = t9_patches.get((r, c))
            if p: p.set_facecolor(T9_COLOR)
        p = t9_patches.get((row, col))
        if p: p.set_facecolor(T9_PRESS)
        _t9_hl["pos"] = (row, col)
        _t9_hl["ts"]  = time.time()
        fig_ctrl.canvas.draw_idle()

    def _find_t9_pos(ch):
        for r, row_data in enumerate(KEYPAD_LAYOUT):
            for c, (main_ch, _) in enumerate(row_data):
                if main_ch == ch:
                    return r, c
        return None

    # ---------------------------------------------------------------- #
    # LCD image                                                         #
    # ---------------------------------------------------------------- #
    canvas_arr = np.full((LCD_HEIGHT, LCD_WIDTH, 3), BG, dtype=np.uint8)
    im = ax_lcd.imshow(canvas_arr, interpolation="nearest", aspect="equal",
                       extent=[0, LCD_WIDTH, LCD_HEIGHT, 0])

    def fb_to_rgb(fb):
        arr    = np.full((LCD_HEIGHT, LCD_WIDTH, 3), BG, dtype=np.uint8)
        fb_arr = np.array(fb, dtype=np.uint8).reshape(6, LCD_WIDTH)
        for page in range(6):
            for bit in range(8):
                y  = page * 8 + bit
                on = (fb_arr[page] >> bit) & 1
                arr[y, on.astype(bool)] = FG
        return arr

    # ---------------------------------------------------------------- #
    # Animation — LCD refresh + highlight expiry                       #
    # ---------------------------------------------------------------- #
    def update(_frame):
        im.set_data(fb_to_rgb(renode.read_mem(fb_addr, LCD_BYTES)))

        now = time.time()
        if _nav_hl["name"] and now - _nav_hl["ts"] > 0.15:
            p = nav_patches.get(_nav_hl["name"])
            if p:
                p.set_facecolor(NAV_COLOR)
                _nav_hl["name"] = None
                fig_ctrl.canvas.draw_idle()
        if _t9_hl["pos"] and now - _t9_hl["ts"] > 0.15:
            r, c = _t9_hl["pos"]
            p = t9_patches.get((r, c))
            if p:
                p.set_facecolor(T9_COLOR)
                _t9_hl["pos"] = None
                fig_ctrl.canvas.draw_idle()

        return [im]

    ani = animation.FuncAnimation(  # noqa: F841 — must stay referenced
        fig_lcd, update,
        interval=int(1000 / args.fps),
        blit=True,
        cache_frame_data=False,
    )

    # ---------------------------------------------------------------- #
    # Keyboard input (works from either window)                        #
    # ---------------------------------------------------------------- #
    def on_key(event):
        nokia_key = KEY_MAP.get(event.key)
        if nokia_key and keys_addr:
            renode.write_byte(keys_addr, nokia_key)
            nav_name = KEYBOARD_TO_NAV.get(event.key)
            if nav_name:
                flash_nav(nav_name)
            return

        char_code = CHAR_KEY_MAP.get(event.key)
        if char_code and char_addr:
            renode.write_byte(char_addr, char_code)
            pos = _find_t9_pos(chr(char_code))
            if pos:
                flash_t9(*pos)

    # ---------------------------------------------------------------- #
    # Mouse click — nav buttons and T9 keypad                          #
    # ---------------------------------------------------------------- #
    def on_click(event):
        if event.xdata is None or event.ydata is None:
            return

        if event.inaxes is ax_nav and keys_addr:
            for name, (cx, cy, bw, bh, _label, key_mask) in NAV_BUTTONS.items():
                if (abs(event.xdata - cx) <= bw / 2 and
                        abs(event.ydata - cy) <= bh / 2):
                    renode.write_byte(keys_addr, key_mask)
                    flash_nav(name)
                    break

        elif event.inaxes is ax_keys and char_addr:
            col = int(event.xdata)
            row = T9_ROWS - 1 - int(event.ydata)
            if 0 <= row < T9_ROWS and 0 <= col < 3:
                ch = KEYPAD_LAYOUT[row][col][0]
                renode.write_byte(char_addr, ord(ch))
                flash_t9(row, col)

    # Connect events to both windows so keyboard works regardless of focus
    for fig in (fig_lcd, fig_ctrl):
        fig.canvas.mpl_connect("key_press_event", on_key)
    fig_ctrl.canvas.mpl_connect("button_press_event", on_click)

    plt.show()


if __name__ == "__main__":
    main()
