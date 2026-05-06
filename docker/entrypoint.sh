#!/usr/bin/env bash
set -euo pipefail

cd /work

# Git safety (required for bind mounts)
git config --global --add safe.directory /work
git config --global --add safe.directory /work/zephyr

if [ ! -d ".west" ]; then
  echo "[docker] Initializing Zephyr workspace (project manifest)..."
  west init -l west-manifest
fi

west update

# West policy: do NOT treat Kconfig warnings as fatal
# west config --local build.kconfig-warnings-as-errors false

ZEPHYR_DIR="$(west list zephyr -f '{path}')"
export ZEPHYR_BASE="${ZEPHYR_DIR}"
source "${ZEPHYR_DIR}/zephyr-env.sh"

CMAKE_ARGS=()
[[ -n "${LCD_OVERLAY:-}" ]]     && CMAKE_ARGS+=("-DOVERLAY_CONFIG=${LCD_OVERLAY}")
[[ -n "${LCD_DTS_OVERLAY:-}" ]] && CMAKE_ARGS+=("-DEXTRA_DTC_OVERLAY_FILE=${LCD_DTS_OVERLAY}")

if [[ ${#CMAKE_ARGS[@]} -gt 0 ]]; then
    exec "$@" -- "${CMAKE_ARGS[@]}"
else
    exec "$@"
fi

