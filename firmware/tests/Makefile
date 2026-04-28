# Standalone host tests — compiles and runs on macOS/Linux with no Zephyr.
# Usage: make test
#        make clean

CC     = cc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -I../app/src

.PHONY: test clean

test: _build/host_test
	@echo ""
	@./_build/host_test

_build/host_test: _build/ui_framework.o _build/test_main.o
	$(CC) $(CFLAGS) $^ -o $@

_build/ui_framework.o: ../app/src/ui_framework.c | _build
	$(CC) $(CFLAGS) -c $< -o $@

_build/test_main.o: ui/src/test_main.c | _build
	$(CC) $(CFLAGS) -c $< -o $@

_build:
	mkdir -p _build

clean:
	rm -rf _build
