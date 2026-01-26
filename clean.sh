#!/usr/bin/env bash
set -euo pipefail

echo "========================================"
echo " Zephyr workspace CLEANUP"
echo "========================================"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

echo "[1/6] Removing build directory"
rm -rf build

echo "[2/6] Removing west workspace metadata"
rm -rf .west

echo "[3/6] Removing Zephyr source tree"
rm -rf zephyr

echo "[4/6] Removing Zephyr modules"
rm -rf modules

echo "[5/6] Removing Zephyr cache (if any)"
rm -rf .cache
rm -rf zephyr/.cache || true

echo "[6/6] Resetting west config (Kconfig warnings, etc)"
if command -v west >/dev/null 2>&1; then
  west config --local --unset build.kconfig-warnings-as-errors || true
fi

echo
echo "========================================"
echo " Cleanup complete."
echo
echo "Next steps:"
echo "  docker/build.sh"
echo "========================================"

