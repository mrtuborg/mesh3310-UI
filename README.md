# Nokia3310-UI

## Build firmware (Docker)

The build system uses **LCD profiles** to target different displays and backends.

### Simulator builds (for Renode)
```bash
./docker/build.sh nokia3310       # 84×48 (original Nokia 3310)
./docker/build.sh sh1107_128      # 128×128 (SH1107 cyan-on-black)
./docker/build.sh ssd1306_128x64  # 128×64 (SSD1306 OLED)
```

### Hardware builds (for XIAO-nRF52840)
```bash
./docker/build.sh hw_sh1107_128   # 128×128 SH1107 over I2C
```

Profile configs live in `firmware/app/lcd_profiles/`. Hardware profiles are
prefixed with `hw_`; all others are simulator profiles (USB is disabled
automatically via DTS overlay).

### Flash to XIAO-nRF52840
1. Double-tap the reset button (board enters bootloader, mounts as `XIAO-SENSE`)
2. Copy the UF2 file:
```bash
cp build/zephyr/zephyr.uf2 /Volumes/XIAO-SENSE/
```

## Run simulator (one command)
```bash
./run-sim.sh                        # default nokia3310 profile
./run-sim.sh --lcd sh1107_128       # SH1107 128×128 cyan display
./run-sim.sh --keypad 4x4_mcp23008  # 4×4 matrix keypad (MCP23008)
./run-sim.sh --scale 8              # custom zoom
./run-sim.sh --fps 15               # custom refresh rate (default: 10)
./run-sim.sh --list-profiles        # show all LCD / keypad profiles
```

The script starts Renode, auto-detects the `nokia_fb` address from the ELF,
and opens the LCD viewer window. Close the window or press Ctrl+C to quit.

> **Note:** Build the matching sim profile first (e.g. `./docker/build.sh sh1107_128`)
> before running `./run-sim.sh --lcd sh1107_128`.

## Run in Renode manually (from project root)
```bash
renode -P 1234 renode/run.resc
```

## Show LCD screen manually (second terminal)
```bash
# Get the nokia_fb address from the ELF:
nm build/zephyr/zephyr.elf | grep nokia_fb

# Launch the viewer (replace address with the one above):
python3 tools/lcd_viewer.py --address 0x2000031d --scale 5 --fps 10
```

## Inject key presses (Renode monitor prompt)
```
sysbus GetSymbolAddress "nokia_keys_raw"
sysbus WriteByte 0x<address> 0x01   # KEY_LEFT  — open menu
sysbus WriteByte 0x<address> 0x08   # KEY_DOWN  — move cursor down
sysbus WriteByte 0x<address> 0x10   # KEY_OK    — select item
sysbus WriteByte 0x<address> 0x02   # KEY_RIGHT — back
sysbus WriteByte 0x<address> 0x04   # KEY_UP    — move cursor up
```

## Run unit tests (no Zephyr required)
```bash
make -C firmware/tests test
```

## Specs
- [Architecture Decision Records](docs/architecture.md)
- [Functional Specification](docs/functional-spec.md)
- [Test Specification](docs/test-spec.md)
