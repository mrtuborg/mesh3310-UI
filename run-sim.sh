#!/usr/bin/env bash
# run-sim.sh — Build (optional), start Renode, and launch the LCD viewer.
#
# Usage:
#   ./run-sim.sh                              # use existing build/zephyr/zephyr.elf
#   ./run-sim.sh --build                      # rebuild firmware via Docker, then exit
#   ./run-sim.sh --scale 5                    # override LCD zoom (default: 8)
#   ./run-sim.sh --fps 15                     # override refresh rate (default: 10)
#   ./run-sim.sh --lcd sh1107_128             # SH1107 128×128 cyan display
#   ./run-sim.sh --keypad 4x4_mcp23008       # 4×4 matrix keypad (MCP23008)
#   ./run-sim.sh --list-profiles              # show all LCD / keypad profiles
set -e
cd "$(dirname "$0")"   # always run from project root

# ------------------------------------------------------------------ #
# Defaults                                                            #
# ------------------------------------------------------------------ #
BUILD=0
SCALE=0
FPS=10
RENODE_PORT=1234
ELF="build/zephyr/zephyr.elf"
LCD_PROFILE="nokia3310"
KEYPAD_PROFILE="nokia"
LIST_PROFILES=0

# ------------------------------------------------------------------ #
# Argument parsing                                                    #
# ------------------------------------------------------------------ #
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build)         BUILD=1 ;;
        --scale)         SCALE="$2";          shift ;;
        --fps)           FPS="$2";            shift ;;
        --port)          RENODE_PORT="$2";    shift ;;
        --lcd)           LCD_PROFILE="$2";    shift ;;
        --keypad)        KEYPAD_PROFILE="$2"; shift ;;
        --list-profiles) LIST_PROFILES=1 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
    shift
done

# ------------------------------------------------------------------ #
# Step 1 — optional firmware build                                    #
# ------------------------------------------------------------------ #
if [[ "$BUILD" == "1" ]]; then
    echo "==> Building firmware (Docker, LCD=${LCD_PROFILE})..."
    ./docker/build.sh "$LCD_PROFILE"
    echo "==> Build complete: $ELF"
    exit 0
fi

# --list-profiles delegates directly to the viewer (no ELF needed)
if [[ "$LIST_PROFILES" == "1" ]]; then
    python3 tools/lcd_viewer.py --list-profiles
    exit 0
fi

if [[ ! -f "$ELF" ]]; then
    echo "ERROR: $ELF not found. Run with --build or build manually first."
    exit 1
fi

# ------------------------------------------------------------------ #
# Step 2 — extract symbol addresses and sizes from ELF               #
# ------------------------------------------------------------------ #
echo "==> Reading symbol addresses from ELF..."

# nm -S prints: <addr> <size> <type> <name>
FB_LINE=$(nm -S "$ELF" | grep -w nokia_fb | head -1)
if [[ -z "$FB_LINE" ]]; then
    echo "ERROR: nokia_fb symbol not found in $ELF"
    exit 1
fi
FB_ADDR="0x$(echo "$FB_LINE" | awk '{print $1}')"
FB_SIZE_HEX=$(echo "$FB_LINE" | awk '{print $2}')
# nm -S may not always print a size; fall back to LCD-profile size
if [[ -n "$FB_SIZE_HEX" && "$FB_SIZE_HEX" =~ ^[0-9a-fA-F]+$ ]]; then
    FB_BYTES=$((16#$FB_SIZE_HEX))
else
    FB_BYTES=""   # let lcd_viewer fall back to profile dimensions
fi
echo "    nokia_fb       @ $FB_ADDR  (${FB_BYTES:-unknown} bytes)"

KEYS_LINE=$(nm "$ELF" | grep -w nokia_keys_raw | head -1)
if [[ -n "$KEYS_LINE" ]]; then
    KEYS_ADDR="0x$(echo "$KEYS_LINE" | awk '{print $1}')"
    echo "    nokia_keys_raw @ $KEYS_ADDR"
else
    KEYS_ADDR=""
    echo "    nokia_keys_raw not found — keyboard input disabled"
fi

CHAR_LINE=$(nm "$ELF" | grep -w nokia_char_raw | head -1)
if [[ -n "$CHAR_LINE" ]]; then
    CHAR_ADDR="0x$(echo "$CHAR_LINE" | awk '{print $1}')"
    echo "    nokia_char_raw @ $CHAR_ADDR"
else
    CHAR_ADDR=""
    echo "    nokia_char_raw not found — text keypad disabled"
fi

# ------------------------------------------------------------------ #
# Step 3 — start Renode in the background                            #
# ------------------------------------------------------------------ #
echo "==> Starting Renode on port $RENODE_PORT..."
renode -P "$RENODE_PORT" renode/run.resc &
RENODE_PID=$!

# Wait for the monitor port to open
echo -n "    Waiting for Renode monitor"
for i in $(seq 1 20); do
    if nc -z 127.0.0.1 "$RENODE_PORT" 2>/dev/null; then
        echo " ready."
        break
    fi
    echo -n "."
    sleep 0.5
    if [[ $i -eq 20 ]]; then
        echo ""
        echo "ERROR: Renode monitor port $RENODE_PORT did not open in time."
        kill "$RENODE_PID" 2>/dev/null
        exit 1
    fi
done

# ------------------------------------------------------------------ #
# Cleanup on exit                                                     #
# ------------------------------------------------------------------ #
cleanup() {
    echo ""
    echo "==> Shutting down Renode (PID $RENODE_PID)..."
    kill "$RENODE_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# ------------------------------------------------------------------ #
# Step 4 — launch LCD viewer (foreground, blocks until window closed) #
# ------------------------------------------------------------------ #
echo "==> Launching LCD viewer (lcd=${LCD_PROFILE}, keypad=${KEYPAD_PROFILE}, scale=${SCALE}x, fps=${FPS})..."
echo "    Controls: ← → ↑ ↓ arrow keys + Enter/Space"
echo "    Close the LCD window or press Ctrl+C to quit."
echo ""

FB_BYTES_ARG=""
[[ -n "$FB_BYTES" ]] && FB_BYTES_ARG="--fb-bytes $FB_BYTES"

KEYS_ARG=""
[[ -n "$KEYS_ADDR" ]] && KEYS_ARG="--keys-address $KEYS_ADDR"

CHAR_ARG=""
[[ -n "$CHAR_ADDR" ]] && CHAR_ARG="--char-address $CHAR_ADDR"

python3 tools/lcd_viewer.py \
    --address "$FB_ADDR" \
    --port    "$RENODE_PORT" \
    --scale   "$SCALE" \
    --fps     "$FPS" \
    --lcd     "$LCD_PROFILE" \
    --keypad  "$KEYPAD_PROFILE" \
    $FB_BYTES_ARG \
    $KEYS_ARG \
    $CHAR_ARG
