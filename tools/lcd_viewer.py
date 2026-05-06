#!/usr/bin/env python3
"""Nokia 3310 UI Simulator — configurable LCD viewer and keypad controller.

LCD and keypad hardware are selectable at runtime via --lcd / --keypad.
Run with --list-profiles to see all options and firmware size requirements.

LCD profiles
------------
  nokia3310        Nokia 3310 PCD8544  (84×48,  green on light-green)
  sh1107_128       SH1107 128×128      (128×128, cyan on black)  ← GME128128-01-IIC
  ssd1306_128x64   SSD1306 OLED        (128×64,  white on black)

Keypad profiles
---------------
  nokia            Nokia T9 3×4 pad + separate nav panel
  4x4_mcp23008     4×4 matrix keypad with MCP23008 I²C expander
"""
import argparse
import socket
import time

import matplotlib
matplotlib.use("macosx")          # native macOS Quartz backend — no Tk required
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from matplotlib.patches import FancyBboxPatch
import numpy as np

# ------------------------------------------------------------------ #
# Nokia key bitmasks (must match ui_framework.h)                     #
# ------------------------------------------------------------------ #
KEY_LEFT  = 0x01
KEY_RIGHT = 0x02
KEY_UP    = 0x04
KEY_DOWN  = 0x08
KEY_OK    = 0x10

# ================================================================== #
# LCD Profiles                                                        #
#                                                                     #
# width × height must match NOKIA_LCD_WIDTH × NOKIA_LCD_HEIGHT in    #
# firmware/app/src/ui_framework.h so the framebuffer sizes align.    #
# ================================================================== #
LCD_PROFILES = {
    "nokia3310": {
        "label":   "Nokia 3310 (PCD8544, 84×48)",
        "width":   84,
        "height":  48,
        "fg":      (27,  47,  27),    # dark green ink
        "bg":      (155, 207, 155),   # light green paper
        "bezel":   "#1a1a1a",
        "fw_note": None,
    },
    "sh1107_128": {
        "label":   "SH1107 128×128 (GME128128-01-IIC, cyan on black)",
        "width":   128,
        "height":  128,
        "fg":      (0,   255, 255),   # cyan
        "bg":      (0,   0,   0),     # black
        "bezel":   "#0a0a0a",
        "fw_note": "Firmware: set NOKIA_LCD_WIDTH=128, NOKIA_LCD_HEIGHT=128 in ui_framework.h",
    },
    "ssd1306_128x64": {
        "label":   "SSD1306 128×64 OLED (white on black)",
        "width":   128,
        "height":  64,
        "fg":      (255, 255, 255),   # white
        "bg":      (0,   0,   0),     # black
        "bezel":   "#0a0a0a",
        "fw_note": "Firmware: set NOKIA_LCD_WIDTH=128, NOKIA_LCD_HEIGHT=64 in ui_framework.h",
    },
}

# ================================================================== #
# Keypad Profiles                                                     #
#                                                                     #
# Each profile:                                                       #
#   label          human-readable name                               #
#   type           "t9" or "matrix4x4"                               #
#   layout         list of rows → list of (main_label, sub_label)   #
#   nav_char_map   main_label → KEY_* for grid keys that drive nav   #
#   nav_buttons    Nokia nav panel dict  (t9 only, else None)        #
#   host_nav_map   host keyboard key-name → KEY_* bitmask            #
#   host_nav_flash host key-name → nav-button-name (t9)             #
#                               or (row, col) tuple  (matrix4x4)    #
# ================================================================== #
KEYPAD_PROFILES = {
    "nokia": {
        "label": "Nokia T9 (3×4 keypad + nav panel)",
        "type":  "t9",
        "layout": [
            [('1', '.,!?'), ('2', 'ABC'),  ('3', 'DEF') ],
            [('4', 'GHI'),  ('5', 'JKL'),  ('6', 'MNO') ],
            [('7', 'PQRS'), ('8', 'TUV'),  ('9', 'WXYZ')],
            [('*', ''),     ('0', '+ '),   ('#', '')     ],
        ],
        "nav_char_map": {},   # no grid key doubles as nav in T9
        # Nokia 3310 physical nav panel  (axes xlim=[0,7], ylim=[0,4])
        # (cx, cy, bw, bh, label, key_mask)
        "nav_buttons": {
            "back":  (1.0, 2.5, 1.6, 0.80, "◄  Back",  KEY_LEFT),
            "up":    (3.5, 3.2, 0.9, 0.70, "▲",         KEY_UP),
            "ok":    (3.5, 2.0, 1.0, 0.90, "●",         KEY_OK),
            "down":  (3.5, 0.8, 0.9, 0.70, "▼",         KEY_DOWN),
            "menu":  (6.0, 2.5, 1.6, 0.80, "Menu  ►",  KEY_RIGHT),
        },
        "host_nav_map": {
            "left":   KEY_LEFT,  "right":  KEY_RIGHT,
            "up":     KEY_UP,    "down":   KEY_DOWN,
            "return": KEY_OK,    " ":      KEY_OK,
        },
        "host_nav_flash": {               # value = nav-button name
            "left": "back",  "right": "menu",
            "up":   "up",    "down":  "down",
            "return": "ok",  " ": "ok",
        },
    },

    "4x4_mcp23008": {
        "label": "4×4 Matrix Keypad (MCP23008 I²C)",
        "type":  "matrix4x4",
        # Sub-label on nav-mapped keys shows the nav function.
        "layout": [
            [('1', ''),       ('2', '↑ ABC'),   ('3', 'DEF'),    ('A', '◄ BCK')],
            [('4', '← GHI'),  ('5', '● JKL'),   ('6', 'MNO →'),  ('B', 'MNU ►')],
            [('7', 'PQRS'),   ('8', '↓ TUV'),   ('9', 'WXYZ'),   ('C', '')     ],
            [('*', ''),       ('0', '+ '),       ('#', ''),        ('D', '')     ],
        ],
        # Keys that inject nokia_keys_raw instead of nokia_char_raw.
        # 2/4/5/6/8 = directional + select;  A/B = soft-key shortcuts.
        "nav_char_map": {
            '2': KEY_UP,   '4': KEY_LEFT,  '5': KEY_OK,
            '6': KEY_RIGHT,'8': KEY_DOWN,
            'A': KEY_LEFT, 'B': KEY_RIGHT,
        },
        "nav_buttons": None,   # no separate nav panel
        "host_nav_map": {
            "left":   KEY_LEFT,  "right":  KEY_RIGHT,
            "up":     KEY_UP,    "down":   KEY_DOWN,
            "return": KEY_OK,    " ":      KEY_OK,
        },
        "host_nav_flash": {               # value = (row, col) in the grid
            "left":   (1, 0),  "right":  (1, 2),   # '4', '6'
            "up":     (0, 1),  "down":   (2, 1),   # '2', '8'
            "return": (1, 1),  " ":      (1, 1),   # '5'
        },
    },
}

# Host keyboard → ASCII char code for nokia_char_raw (text entry)
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
    ap = argparse.ArgumentParser(
        description="Nokia 3310 UI Simulator — configurable LCD and keypad.")
    ap.add_argument("--address",      required=False, default=None,
                    type=lambda x: int(x, 0), help="nokia_fb RAM address")
    ap.add_argument("--keys-address", required=False, default=None,
                    type=lambda x: int(x, 0), help="nokia_keys_raw address (nav keys)")
    ap.add_argument("--char-address", required=False, default=None,
                    type=lambda x: int(x, 0), help="nokia_char_raw address (text keypad)")
    ap.add_argument("--host",   default="127.0.0.1")
    ap.add_argument("--port",   default=1234,        type=int)
    ap.add_argument("--scale",  default=0,           type=int,
                    help="LCD pixel scale (0 = auto-fit to screen, default: 0)")
    ap.add_argument("--fps",    default=10,          type=int)
    ap.add_argument("--lcd",    default="nokia3310",
                    choices=list(LCD_PROFILES.keys()), metavar="PROFILE",
                    help="LCD profile  (default: nokia3310)")
    ap.add_argument("--keypad", default="nokia",
                    choices=list(KEYPAD_PROFILES.keys()), metavar="PROFILE",
                    help="Keypad profile (default: nokia)")
    ap.add_argument("--fb-bytes", default=None, type=int,
                    help="Exact framebuffer size in bytes read from the ELF "
                         "(auto-detected by run-sim.sh; overrides LCD profile size)")
    ap.add_argument("--list-profiles", action="store_true",
                    help="List available LCD / keypad profiles and exit")
    args = ap.parse_args()

    if args.list_profiles:
        print("LCD profiles:")
        for k, v in LCD_PROFILES.items():
            w, h = v["width"], v["height"]
            print(f"  {k:<22} {v['label']}  ({w}×{h}, fb={w*h//8} bytes)")
            if v.get("fw_note"):
                print(f"    ⚠  {v['fw_note']}")
        print()
        print("Keypad profiles:")
        for k, v in KEYPAD_PROFILES.items():
            print(f"  {k:<22} {v['label']}")
        return

    if args.address is None:
        ap.error("--address is required")

    lp = LCD_PROFILES[args.lcd]
    kp = KEYPAD_PROFILES[args.keypad]

    LCD_W     = lp["width"]
    LCD_H     = lp["height"]
    LCD_BYTES = LCD_W * LCD_H // 8   # full display framebuffer size

    # Actual firmware framebuffer size (may differ from LCD profile if firmware
    # hasn't been updated yet).  run-sim.sh reads this from the ELF symbol table.
    FW_BYTES = args.fb_bytes if args.fb_bytes else LCD_BYTES

    if FW_BYTES != LCD_BYTES:
        fw_pages = FW_BYTES * 8 // LCD_W   # rows the firmware actually renders
        fw_h     = fw_pages
        fw_w     = min(FW_BYTES * 8 // fw_pages, LCD_W) if fw_pages else LCD_W
        print(f"\n⚠  Firmware framebuffer ({FW_BYTES} bytes = {fw_w}×{fw_h}) is smaller "
              f"than the LCD profile ({LCD_W}×{LCD_H}).  "
              f"Content will appear in the top-left of the display.\n"
              f"   Rebuild with the matching LCD profile to fill the full screen:\n"
              f"     ./run-sim.sh --build --lcd {args.lcd}\n")
    elif lp.get("fw_note"):
        print(f"\n⚠  {lp['fw_note']}\n")
    FG        = np.array(lp["fg"], dtype=np.uint8)
    BG        = np.array(lp["bg"], dtype=np.uint8)

    renode    = RenodeClient(args.host, args.port)
    fb_addr   = args.address
    keys_addr = args.keys_address
    char_addr = args.char_address

    # ================================================================ #
    # Scale + sizing                                                   #
    # 0 → auto-fit: choose the largest integer scale that keeps the   #
    # combined window within 85 % of the screen height.               #
    # ================================================================ #
    dpi = 100
    if args.scale == 0:
        try:
            import tkinter as _tk
            _r = _tk.Tk(); _r.withdraw()
            _sh = _r.winfo_screenheight()
            _sw = _r.winfo_screenwidth()
            _r.destroy()
        except Exception:
            _sh, _sw = 900, 1440
        _avail_h = int(_sh * 0.85)
        _avail_w = int(_sw * 0.80)
        # Keypad occupies roughly as much width as the LCD
        _s_h = max(1, _avail_h // LCD_H)
        _s_w = max(1, _avail_w // (LCD_W * 2))
        S = min(_s_h, _s_w, 8)
    else:
        S = args.scale

    matplotlib.rcParams["toolbar"] = "None"

    BEZEL    = max(6, S)
    GAP      = max(8, S)          # gap between LCD panel and keypad panel
    PAD      = max(6, S)          # right padding

    lcd_w_px = LCD_W * S
    lcd_h_px = LCD_H * S

    CS       = min(S, 5)          # keypad cell scale (capped to avoid giant buttons)
    ctrl_pad = max(10, CS * 2)

    # ── pre-compute keypad panel dimensions ─────────────────────────── #
    if kp["type"] == "t9":
        nav_h   = max(150, CS * 30)
        t9_h    = max(220, CS * 44)
        ctrl_w  = max(160, LCD_W * CS + BEZEL * 2)
        ctrl_h  = ctrl_pad + nav_h + ctrl_pad // 2 + t9_h + ctrl_pad
        t9_bot  = ctrl_pad
        nav_bot = ctrl_pad + t9_h + ctrl_pad // 2
        GRID_ROWS = len(kp["layout"])
        GRID_COLS = len(kp["layout"][0])
    elif kp["type"] == "matrix4x4":
        GRID_ROWS = len(kp["layout"])
        GRID_COLS = len(kp["layout"][0])
        hdr_px    = 22
        cell_px   = max(65, CS * 14)
        ctrl_w    = GRID_COLS * cell_px + ctrl_pad * 2
        ctrl_h    = GRID_ROWS * cell_px + ctrl_pad * 2 + hdr_px
    else:
        ctrl_w = ctrl_h = 0

    # ── combined figure dimensions ───────────────────────────────────── #
    total_w   = BEZEL + lcd_w_px + GAP + ctrl_w + PAD
    total_h   = BEZEL * 2 + max(lcd_h_px, ctrl_h)

    # Vertical centre offsets for each panel
    lcd_bot_px  = (total_h - lcd_h_px) / 2
    ctrl_bot_px = (total_h - ctrl_h) / 2
    ctrl_x_px   = BEZEL + lcd_w_px + GAP   # left edge of keypad region

    # ================================================================ #
    # Single combined window                                           #
    # ================================================================ #
    fig = plt.figure(f"Sim — {lp['label']}",
                     figsize=(total_w / dpi, total_h / dpi), dpi=dpi)
    fig.patch.set_facecolor("#1a1a1a")

    fig.set_size_inches(total_w / dpi, total_h / dpi, forward=True)
    # Disable window resizing via the backend window manager.
    try:
        fig.canvas.manager.window.resizable(False, False)   # Tk backend
    except AttributeError:
        try:
            fig.canvas.manager.window.set_resizable(False)  # GTK backend
        except AttributeError:
            pass  # Qt or other backends — best-effort

    # Bezel background rectangle behind the LCD panel
    from matplotlib.patches import Rectangle as _Rect
    _bz = _Rect(
        (0, (lcd_bot_px - BEZEL) / total_h),
        (BEZEL + lcd_w_px + BEZEL * 0.5) / total_w,
        (lcd_h_px + BEZEL * 2) / total_h,
        transform=fig.transFigure,
        facecolor=lp["bezel"], edgecolor="none", zorder=0,
    )
    fig.add_artist(_bz)

    # LCD image axes
    ax_lcd = fig.add_axes([
        BEZEL / total_w,
        lcd_bot_px / total_h,
        lcd_w_px / total_w,
        lcd_h_px / total_h,
    ])
    ax_lcd.axis("off")

    KEY_PRESS_COLOR = "#5a9e5a"
    NAV_MAPPED      = set(kp["nav_char_map"].keys())

    GRID_BW  = 0.84
    GRID_BH  = 0.76
    GRID_BX  = (1 - GRID_BW) / 2
    GRID_BY  = (1 - GRID_BH) / 2

    nav_patches  = {}
    grid_patches = {}
    ax_nav       = None
    ax_grid      = None

    _nav_hl  = {"name": None, "ts": 0.0}
    _grid_hl = {"pos":  None, "ts": 0.0}

    # helper: convert pixel coords relative to keypad region bottom-left
    # into figure fractions
    def _kp_axes(rel_left_px, rel_bot_px, w_px, h_px):
        return [
            (ctrl_x_px + rel_left_px) / total_w,
            (ctrl_bot_px + rel_bot_px) / total_h,
            w_px / total_w,
            h_px / total_h,
        ]

    # ── T9 layout ──────────────────────────────────────────────────── #
    if kp["type"] == "t9":
        MX_px = ctrl_pad

        NAV_COLOR = "#3a3a3a" if keys_addr else "#262626"

        ax_nav = fig.add_axes(_kp_axes(MX_px, nav_bot, ctrl_w - 2 * MX_px, nav_h))
        ax_nav.set_facecolor("#1e1e1e")
        ax_nav.set_xlim(0, 7)
        ax_nav.set_ylim(0, 4)
        ax_nav.axis("off")

        for name, (cx, cy, bw, bh, label, _mask) in kp["nav_buttons"].items():
            patch = FancyBboxPatch(
                (cx - bw / 2, cy - bh / 2), bw, bh,
                boxstyle="round,pad=0.06",
                facecolor=NAV_COLOR, edgecolor="#606060",
                linewidth=0.9, zorder=2,
            )
            ax_nav.add_patch(patch)
            nav_patches[name] = patch
            fs = 11 if name == "ok" else (8 if name in ("back", "menu") else 12)
            ax_nav.text(cx, cy, label, ha="center", va="center",
                        color="#e0e0e0" if keys_addr else "#484848",
                        fontsize=fs, fontweight="bold", zorder=3)

        if not keys_addr:
            ax_nav.text(3.5, 0.2, "Pass --keys-address to enable nav keys",
                        ha="center", va="center", color="#484848", fontsize=5.5)

        T9_COLOR = "#3a3a3a" if char_addr else "#262626"

        ax_grid = fig.add_axes(_kp_axes(MX_px, t9_bot, ctrl_w - 2 * MX_px, t9_h))
        ax_grid.set_facecolor("#212121")
        ax_grid.set_xlim(0, GRID_COLS)
        ax_grid.set_ylim(0, GRID_ROWS)
        ax_grid.axis("off")

        for row, key_row in enumerate(kp["layout"]):
            y_base = GRID_ROWS - 1 - row
            for col, (main_ch, letters) in enumerate(key_row):
                patch = FancyBboxPatch(
                    (col + GRID_BX, y_base + GRID_BY), GRID_BW, GRID_BH,
                    boxstyle="round,pad=0.03",
                    facecolor=T9_COLOR, edgecolor="#505050",
                    linewidth=0.8, zorder=2,
                )
                ax_grid.add_patch(patch)
                grid_patches[(row, col)] = patch
                ax_grid.text(col + 0.5, y_base + 0.70, main_ch,
                             ha="center", va="center",
                             color="#e0e0e0" if char_addr else "#484848",
                             fontsize=10, fontweight="bold",
                             fontfamily="monospace", zorder=3)
                if letters:
                    ax_grid.text(col + 0.5, y_base + 0.28, letters,
                                 ha="center", va="center",
                                 color="#888888" if char_addr else "#3a3a3a",
                                 fontsize=5, fontfamily="monospace", zorder=3)

        if not char_addr:
            ax_grid.text(GRID_COLS / 2, -0.14,
                         "Pass --char-address to enable text keypad",
                         ha="center", va="center", color="#484848", fontsize=5.5)

        def _grid_default_color(row, col):
            return T9_COLOR

        def _flash_nav(name):
            if _nav_hl["name"]:
                p = nav_patches.get(_nav_hl["name"])
                if p: p.set_facecolor(NAV_COLOR)
            p = nav_patches.get(name)
            if p: p.set_facecolor(KEY_PRESS_COLOR)
            _nav_hl["name"] = name
            _nav_hl["ts"]   = time.time()
            fig.canvas.draw_idle()

    # ── 4×4 matrix layout ──────────────────────────────────────────── #
    elif kp["type"] == "matrix4x4":
        _enabled = keys_addr or char_addr
        _C_NAVONLY = "#1e2a1e"
        _C_DUAL    = "#1e2d3a"
        _C_PLAIN   = "#3a3a3a"
        _C_DIM     = "#262626"
        _E_NAV     = "#4a7a6a"
        _E_PLAIN   = "#505050"

        def _cell_colors(main_ch):
            if not _enabled:
                return _C_DIM, _E_PLAIN
            if main_ch in NAV_MAPPED:
                return (_C_DUAL if main_ch.isdigit() or main_ch in '*#'
                        else _C_NAVONLY), _E_NAV
            return _C_PLAIN, _E_PLAIN

        # Header label
        hdr_cx = (ctrl_x_px + ctrl_w / 2) / total_w
        hdr_cy = (ctrl_bot_px + ctrl_h - hdr_px / 2) / total_h
        fig.text(hdr_cx, hdr_cy, kp["label"],
                 ha="center", va="center", color="#666666", fontsize=6,
                 transform=fig.transFigure)

        ax_grid = fig.add_axes(_kp_axes(ctrl_pad, ctrl_pad,
                                        ctrl_w - 2 * ctrl_pad,
                                        ctrl_h - 2 * ctrl_pad - hdr_px))
        ax_grid.set_facecolor("#1a1a1a")
        ax_grid.set_xlim(0, GRID_COLS)
        ax_grid.set_ylim(0, GRID_ROWS)
        ax_grid.axis("off")

        for row, key_row in enumerate(kp["layout"]):
            y_base = GRID_ROWS - 1 - row
            for col, (main_ch, sub_lbl) in enumerate(key_row):
                fc, ec = _cell_colors(main_ch)
                patch = FancyBboxPatch(
                    (col + GRID_BX, y_base + GRID_BY), GRID_BW, GRID_BH,
                    boxstyle="round,pad=0.04",
                    facecolor=fc, edgecolor=ec, linewidth=0.9, zorder=2,
                )
                ax_grid.add_patch(patch)
                grid_patches[(row, col)] = patch

                main_color = "#e0e0e0" if _enabled else "#484848"
                ax_grid.text(col + 0.5, y_base + 0.65, main_ch,
                             ha="center", va="center", color=main_color,
                             fontsize=11, fontweight="bold",
                             fontfamily="monospace", zorder=3)
                if sub_lbl:
                    sub_color = ("#00cccc" if main_ch in NAV_MAPPED else "#777777") \
                                if _enabled else "#3a3a3a"
                    ax_grid.text(col + 0.5, y_base + 0.22, sub_lbl,
                                 ha="center", va="center", color=sub_color,
                                 fontsize=4, fontfamily="monospace", zorder=3)

        if not _enabled:
            ax_grid.text(GRID_COLS / 2, -0.2,
                         "Pass --keys-address / --char-address to enable",
                         ha="center", va="center", color="#484848", fontsize=5)

        def _grid_default_color(row, col):
            return _cell_colors(kp["layout"][row][col][0])[0]

        def _flash_nav(_name):
            pass   # no separate nav panel in matrix4x4 mode

    # ================================================================ #
    # Shared: grid flash helper                                        #
    # ================================================================ #
    def flash_grid(row, col):
        if _grid_hl["pos"]:
            r, c = _grid_hl["pos"]
            p = grid_patches.get((r, c))
            if p: p.set_facecolor(_grid_default_color(r, c))
        p = grid_patches.get((row, col))
        if p: p.set_facecolor(KEY_PRESS_COLOR)
        _grid_hl["pos"] = (row, col)
        _grid_hl["ts"]  = time.time()
        fig.canvas.draw_idle()

    def _find_grid_pos(ch):
        for r, row_data in enumerate(kp["layout"]):
            for c, (main_ch, _) in enumerate(row_data):
                if main_ch == ch:
                    return r, c
        return None

    # ================================================================ #
    # LCD image + framebuffer decode                                   #
    # ================================================================ #
    canvas_arr = np.full((LCD_H, LCD_W, 3), BG, dtype=np.uint8)
    im = ax_lcd.imshow(canvas_arr, interpolation="nearest", aspect="equal",
                       extent=[0, LCD_W, LCD_H, 0])

    def fb_to_rgb(fb):
        arr = np.full((LCD_H, LCD_W, 3), BG, dtype=np.uint8)

        fw_bytes = len(fb)
        fw_cols, fw_pages = LCD_W, 0
        for try_w in sorted({LCD_W, 128, 84}, key=lambda w: 0 if w == LCD_W else 1):
            if try_w > 0 and fw_bytes % try_w == 0:
                pages = fw_bytes // try_w
                if 0 < pages <= 16:
                    fw_cols, fw_pages = try_w, pages
                    break

        if fw_pages == 0:
            return arr

        fb_arr = np.array(fb[:fw_pages * fw_cols], dtype=np.uint8).reshape(fw_pages, fw_cols)
        dest_w = min(fw_cols, LCD_W)
        for page in range(fw_pages):
            for bit in range(8):
                y = page * 8 + bit
                if y >= LCD_H:
                    break
                on = (fb_arr[page, :dest_w] >> bit) & 1
                arr[y, :dest_w][on.astype(bool)] = FG
        return arr

    # ================================================================ #
    # Animation — LCD refresh + highlight expiry                       #
    # ================================================================ #
    def update(_frame):
        im.set_data(fb_to_rgb(renode.read_mem(fb_addr, FW_BYTES)))

        now = time.time()
        if _nav_hl["name"] and now - _nav_hl["ts"] > 0.15:
            p = nav_patches.get(_nav_hl["name"])
            if p:
                p.set_facecolor(NAV_COLOR if kp["type"] == "t9" else "#262626")
                _nav_hl["name"] = None
                fig.canvas.draw_idle()
        if _grid_hl["pos"] and now - _grid_hl["ts"] > 0.15:
            r, c = _grid_hl["pos"]
            p = grid_patches.get((r, c))
            if p:
                p.set_facecolor(_grid_default_color(r, c))
                _grid_hl["pos"] = None
                fig.canvas.draw_idle()

        return [im]

    ani = animation.FuncAnimation(
        fig, update,
        interval=int(1000 / args.fps),
        blit=True,
        cache_frame_data=False,
    )

    # ================================================================ #
    # Keyboard input                                                   #
    # ================================================================ #
    def on_key(event):
        nokia_key = kp["host_nav_map"].get(event.key)
        if nokia_key and keys_addr:
            renode.write_byte(keys_addr, nokia_key)
            flash_info = kp["host_nav_flash"].get(event.key)
            if flash_info is not None:
                if kp["type"] == "t9":
                    _flash_nav(flash_info)
                else:
                    flash_grid(*flash_info)
            return

        char_code = CHAR_KEY_MAP.get(event.key)
        if char_code and char_addr:
            renode.write_byte(char_addr, char_code)
            pos = _find_grid_pos(chr(char_code))
            if pos:
                flash_grid(*pos)

    # ================================================================ #
    # Mouse click — keypad buttons                                     #
    # ================================================================ #
    def on_click(event):
        if event.xdata is None or event.ydata is None:
            return

        if kp["type"] == "t9":
            if event.inaxes is ax_nav and keys_addr:
                for name, (cx, cy, bw, bh, _lbl, mask) in kp["nav_buttons"].items():
                    if abs(event.xdata - cx) <= bw / 2 and \
                            abs(event.ydata - cy) <= bh / 2:
                        renode.write_byte(keys_addr, mask)
                        _flash_nav(name)
                        break
            elif event.inaxes is ax_grid and char_addr:
                col = int(event.xdata)
                row = GRID_ROWS - 1 - int(event.ydata)
                if 0 <= row < GRID_ROWS and 0 <= col < GRID_COLS:
                    ch = kp["layout"][row][col][0]
                    renode.write_byte(char_addr, ord(ch))
                    flash_grid(row, col)

        elif kp["type"] == "matrix4x4" and event.inaxes is ax_grid:
            col = int(event.xdata)
            row = GRID_ROWS - 1 - int(event.ydata)
            if 0 <= row < GRID_ROWS and 0 <= col < GRID_COLS:
                ch       = kp["layout"][row][col][0]
                nav_mask = kp["nav_char_map"].get(ch)
                if nav_mask and keys_addr:
                    renode.write_byte(keys_addr, nav_mask)
                    flash_grid(row, col)
                elif not nav_mask and char_addr:
                    renode.write_byte(char_addr, ord(ch))
                    flash_grid(row, col)

    fig.canvas.mpl_connect("key_press_event", on_key)
    fig.canvas.mpl_connect("button_press_event", on_click)

    plt.show()


if __name__ == "__main__":
    main()
