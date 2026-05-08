#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "ui_framework.h"
#include "display.h"
#ifdef CONFIG_NOKIA_KEYPAD_MCP23008
#include "keypad_mcp23008.h"
#endif
#ifdef CONFIG_USB_DEVICE_STACK
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>
#endif

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

/*
 * Character key input for text entry (e.g. SMS/T9).
 * lcd_viewer.py writes the ASCII code of the pressed key (0-9, *, #).
 * Cleared by the consumer after reading.  Unused until SMS is implemented.
 */
__attribute__((used))
volatile uint8_t nokia_char_raw;

/* Defined in screens.c */
extern const screen_def_t g_screens[];
extern const int          g_screen_count;

int main(void)
{
#ifdef CONFIG_USB_DEVICE_STACK
    /* Enable USB and wait up to 3 s for a host terminal (DTR).
     * The board continues normally if nobody connects. */
    usb_enable(NULL);
    const struct device *cdc = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    if (device_is_ready(cdc)) {
        uint32_t dtr = 0;
        for (int i = 0; i < 30 && !dtr; i++) {
            uart_line_ctrl_get(cdc, UART_LINE_CTRL_DTR, &dtr);
            k_sleep(K_MSEC(100));
        }
    }
#endif

    printk("Nokia3310 UI starting...\n");

    if (display_init() != 0) {
        printk("Warning: display init failed, continuing anyway\n");
    }

#ifdef CONFIG_NOKIA_KEYPAD_MCP23008
    if (keypad_mcp23008_init() != 0) {
        printk("Warning: keypad init failed, hardware keys disabled\n");
    }
#endif

    ui_init(g_screens, g_screen_count, 0);

    while (1) {
        uint8_t k = nokia_keys_raw;
        if (k) {
            nokia_keys_raw = 0;
            ui_inject_key((nokia_key_t)k);
        }

#ifdef CONFIG_NOKIA_KEYPAD_MCP23008
        nokia_key_t hw_key = keypad_mcp23008_poll();
        if (hw_key != KEY_NONE) {
            ui_inject_key(hw_key);
        }
#endif

        ui_tick();
        display_flush();
        k_sleep(K_MSEC(5));
    }

    return 0;
}


