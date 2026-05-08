#!/usr/bin/env bash
set -e
LCD_PROFILE="${1:-}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"

# Clear any inherited env from prior invocations in the same shell.
unset LCD_OVERLAY
unset LCD_DTS_OVERLAY

# Always pass the profile .conf as OVERLAY_CONFIG (including nokia3310).
if [[ -n "$LCD_PROFILE" ]]; then
    export LCD_OVERLAY="/work/firmware/app/lcd_profiles/${LCD_PROFILE}.conf"
fi

# Hardware profiles are prefixed with hw_; everything else is a sim profile.
if [[ "$LCD_PROFILE" == hw_* ]]; then
    HW_OVERLAY="$REPO_ROOT/firmware/app/lcd_profiles/${LCD_PROFILE}.overlay"
    if [[ -f "$HW_OVERLAY" ]]; then
        export LCD_DTS_OVERLAY="/work/firmware/app/lcd_profiles/${LCD_PROFILE}.overlay"
    else
        export LCD_DTS_OVERLAY=""
    fi
else
    # Simulator build: disable USBD node to prevent the nRF USB driver from
    # busy-looping on USBD:EVENTCAUSE which Renode does not simulate.
    export LCD_DTS_OVERLAY="/work/firmware/app/lcd_profiles/sim_no_usb.overlay"
fi

# Always pristine-build when switching between hw and sim (or vice-versa)
# because CMake caches the DTS overlay and Kconfig overlay paths.
if [[ -d "$BUILD_DIR" ]]; then
    CACHED_DTS_OVERLAY=""
    if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        CACHED_DTS_OVERLAY="$(grep -o 'sim_no_usb.overlay' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null || true)"
    fi
    if [[ "$LCD_PROFILE" == hw_* && -n "$CACHED_DTS_OVERLAY" ]]; then
        echo "==> Cleaning stale sim build (has USB-disable overlay; hw needs USB)..."
        rm -rf "$BUILD_DIR"
    elif [[ "$LCD_PROFILE" != hw_* && -z "$CACHED_DTS_OVERLAY" && -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        echo "==> Cleaning stale hw build (no USB-disable overlay; sim needs it)..."
        rm -rf "$BUILD_DIR"
    fi
fi

docker compose -f "$(dirname "$0")/docker-compose.yml" run --rm zephyr

