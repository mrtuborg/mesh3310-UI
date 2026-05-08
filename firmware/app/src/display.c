#include "display.h"
#include "display_sh1107.h"

#ifdef CONFIG_NOKIA_BACKEND_SH1107_I2C

#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

#include "display_transform.h"
#include "ui_framework.h"

static const struct gpio_dt_spec leds[] = {
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios),
};

static bool display_ready;

static void led_set(int idx, bool on)
{
    if (device_is_ready(leds[idx].port)) {
        gpio_pin_set_dt(&leds[idx], on ? 1 : 0);
    }
}

static void led_init_all(void)
{
    for (int i = 0; i < 3; i++) {
        if (device_is_ready(leds[i].port)) {
            gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
        }
    }
}

static bool display_is_ready(void)
{
    return display_ready;
}

int display_init(void)
{
    led_init_all();

    for (int i = 0; i < 3; i++) {
        led_set(i, true);
    }
    k_sleep(K_MSEC(300));
    for (int i = 0; i < 3; i++) {
        led_set(i, false);
    }
    k_sleep(K_MSEC(200));

    int ret = sh1107_hw_init();
    if (ret == 0) {
        display_ready = true;
        led_set(1, true);  /* green = OK */
        return 0;
    }

    display_ready = false;
    printk("display: init failed (%d)\n", ret);

    if (ret == -ENOENT) {
        /* Nothing on bus — rapid red × 10 */
        for (int i = 0; i < 10; i++) {
            led_set(0, true);  k_sleep(K_MSEC(100));
            led_set(0, false); k_sleep(K_MSEC(100));
        }
    } else if (ret == -ENXIO) {
        /* Bus has devices but no SH1107 — red × 6 */
        for (int i = 0; i < 6; i++) {
            led_set(0, true);  k_sleep(K_MSEC(150));
            led_set(0, false); k_sleep(K_MSEC(150));
        }
    }
    led_set(0, true);   /* red = failed */
    return ret;
}

void display_flush(void)
{
    if (!display_is_ready()) {
        return;
    }

    static bool hb;
    static int err_count;
    uint8_t buf[NOKIA_LCD_WIDTH];

    hb = !hb;
    led_set(2, hb);

    for (int page = 0; page < NOKIA_LCD_PAGES; page++) {
        int ret;

        display_transform_page(page, buf);
        ret = sh1107_write_page(page, buf);
        if (ret) {
            err_count++;
            printk("display: page %d failed (%d) [%d]\n",
                   page, ret, err_count);

            /* Try to unstick the bus */
            const struct device *bus = DEVICE_DT_GET(DT_NODELABEL(i2c1));
            i2c_recover_bus(bus);
            k_sleep(K_MSEC(5));

            if (err_count >= 20) {
                display_ready = false;
                led_set(1, false);
                led_set(0, true);
                printk("display: too many errors, disabling\n");
            }
            return;
        }
    }
    err_count = 0;
}

#endif
