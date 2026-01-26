#!/usr/bin/env python3
import argparse
import socket
import time
import tkinter as tk

LCD_WIDTH  = 84
LCD_HEIGHT = 48
LCD_BYTES  = LCD_WIDTH * LCD_HEIGHT // 8  # 504 bytes


class RenodeClient:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port))
        self.sock.settimeout(1.0)

    def read_mem(self, addr, size):
        # Ask Renode to dump bytes
        cmd = f"sysbus ReadBytes 0x{addr:x} {size}\n"
        self.sock.sendall(cmd.encode("ascii"))

        data = b""
        deadline = time.time() + 0.5

        while time.time() < deadline:
            try:
                chunk = self.sock.recv(4096)
                if not chunk:
                    break
                data += chunk
                if b"(monitor)" in data or b"(nrf" in data:
                    break
            except socket.timeout:
                break

        out = []

        text = data.decode(errors="ignore")
        for tok in text.replace(",", " ").replace("[", " ").replace("]", " ").split():
            if tok.startswith("0x"):
                try:
                    out.append(int(tok, 16))
                except ValueError:
                    pass

        if len(out) < size:
            print(f"[lcd] Warning: short framebuffer read {len(out)}/{size}, padding")
            out.extend([0x00] * (size - len(out)))

        return out[:size]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--address", required=True, type=lambda x: int(x, 0))
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", default=1234, type=int)
    ap.add_argument("--scale", default=4, type=int)
    ap.add_argument("--fps", default=10, type=int)
    args = ap.parse_args()

    print(f"[lcd] Connecting to Renode at {args.host}:{args.port}")
    renode = RenodeClient(args.host, args.port)
    addr = args.address

    root = tk.Tk()
    root.title("Nokia 3310 LCD")

    img = tk.PhotoImage(width=LCD_WIDTH, height=LCD_HEIGHT)
    lbl = tk.Label(root)
    lbl.pack()

    # Nokia 3310 colors
    FG = "#1b2f1b"   # dark green
    BG = "#9bcf9b"   # light green

    def update():
        fb = renode.read_mem(addr, LCD_BYTES)

        if len(fb) != LCD_BYTES:
            print(f"[lcd] Warning: short framebuffer read {len(fb)}/{LCD_BYTES}, padding")
            fb = fb + [0x00] * (LCD_BYTES - len(fb))

        # Draw row-by-row (FAST)
        for y in range(LCD_HEIGHT):
            page = y // 8
            bit = y % 8
            base = page * LCD_WIDTH

            row = []
            for x in range(LCD_WIDTH):
                pixel_on = (fb[base + x] >> bit) & 1
                row.append(FG if pixel_on else BG)

            img.put("{" + " ".join(row) + "}", to=(0, y))

        # Scale once per frame
        if args.scale != 1:
            scaled = img.zoom(args.scale, args.scale)
            lbl.configure(image=scaled)
            lbl.image = scaled
        else:
            lbl.configure(image=img)
            lbl.image = img

        root.after(int(1000 / args.fps), update)

    update()
    root.mainloop()


if __name__ == "__main__":
    main()
