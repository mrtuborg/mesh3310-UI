#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "ui_framework.h"

/*
 * Memory-mapped button state.
 * Write one of the KEY_* values (see ui_framework.h) here from Renode /
 * Python to simulate a key press.  The main loop consumes and clears it.
 *
 * Example from Python / Renode monitor:
 *   sysbus.WriteByteToAddress(nokia_keys_raw_address, 0x01)  # KEY_LEFT
 */
__attribute__((used))
volatile uint8_t nokia_keys_raw;

/* Defined in screens.c */
extern const screen_def_t g_screens[];
extern const int          g_screen_count;

int main(void)
{
    printk("Nokia3310 UI starting...\n");

    ui_init(g_screens, g_screen_count, 0);

    while (1) {
        uint8_t k = nokia_keys_raw;
        if (k) {
            nokia_keys_raw = 0;
            ui_inject_key((nokia_key_t)k);
        }

        ui_tick();
        k_sleep(K_MSEC(100));
    }

    return 0;
}


