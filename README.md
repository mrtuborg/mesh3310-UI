# Nokia3310-UI

## Build firmware (Docker)
```bash
./docker/build.sh
# produces build/zephyr/zephyr.elf
```

## Run simulator (one command)
```bash
./run-sim.sh              # use existing build
./run-sim.sh --build      # rebuild firmware via Docker and exit
./run-sim.sh --scale 8    # custom zoom (default: 5)
./run-sim.sh --fps 15     # custom refresh rate (default: 10)
```

The script starts Renode, auto-detects the `nokia_fb` address, and opens
the LCD viewer window. Close the window or press Ctrl+C to quit.
Use `--build` separately to rebuild, then run `./run-sim.sh` to simulate.

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
- [Functional Specification](docs/functional-spec.md)
- [Test Specification](docs/test-spec.md)
