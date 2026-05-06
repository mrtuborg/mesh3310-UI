#pragma once

/*
 * Unified display interface.
 * Works for SH1107 I2C hardware and simulator (no-ops for sim).
 */
#ifdef CONFIG_NOKIA_BACKEND_SH1107_I2C
int display_init(void);
void display_flush(void);
#else
static inline int display_init(void)
{
    return 0;
}

static inline void display_flush(void)
{
}
#endif
