#!/usr/bin/env bash
set -euo pipefail

BOARD="nrf52840dk/nrf52840"

docker run --rm -it \
  -v "$(pwd)":/work \
  -w /work \
  ghcr.io/zephyrproject-rtos/zephyr-build:latest \
  west build -b ${BOARD} firmware/app -d build
