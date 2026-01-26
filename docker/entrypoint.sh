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

exec "$@"

