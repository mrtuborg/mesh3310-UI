#pragma once

#include <stdint.h>

/*
 * Pure SH1107 I2C protocol layer.
 * No rotation, no framebuffer logic.
 */
int sh1107_hw_init(void);
int sh1107_write_page(int page, const uint8_t *buf);
