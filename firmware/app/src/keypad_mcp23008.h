/*
 * 4×4 matrix keypad driver via MCP23008 I²C GPIO expander.
 *
 * Wiring (default):
 *   GP0–GP3 : rows    (output, driven LOW one at a time)
 *   GP4–GP7 : columns (input,  internal pull-ups enabled)
 *
 * Call keypad_mcp23008_init() once at startup.
 * Call keypad_mcp23008_poll() every main-loop tick to get the current key.
 * Returns KEY_NONE when no navigation key is pressed.
 */
#pragma once

#ifdef CONFIG_NOKIA_KEYPAD_MCP23008

#include "ui_framework.h"

int          keypad_mcp23008_init(void);
nokia_key_t  keypad_mcp23008_poll(void);

#endif /* CONFIG_NOKIA_KEYPAD_MCP23008 */
