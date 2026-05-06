#include "display_sh1107.h"

#ifdef CONFIG_NOKIA_BACKEND_SH1107_I2C

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include "ui_framework.h"

#define CTRL_CMD_STREAM  0x00
#define CTRL_DATA_STREAM 0x40

static const struct device *i2c_dev;
static uint8_t sh1107_addr;

static int sh1107_cmds(const uint8_t *cmds, size_t len)
{
    uint8_t buf[32];

    if (len + 1 > sizeof(buf)) {
        return -EINVAL;
    }

    buf[0] = CTRL_CMD_STREAM;
    memcpy(&buf[1], cmds, len);
    return i2c_write(i2c_dev, buf, len + 1, sh1107_addr);
}

static int sh1107_probe(uint8_t addr)
{
    uint8_t dummy;

    return i2c_read(i2c_dev, &dummy, 1, addr);
}

static void sh1107_fill(uint8_t byte)
{
    for (int page = 0; page < NOKIA_LCD_PAGES; page++) {
        uint8_t addr_cmds[] = { 0xB0 | (uint8_t)page, 0x00, 0x10 };
        uint8_t buf[1 + NOKIA_LCD_WIDTH];

        sh1107_cmds(addr_cmds, sizeof(addr_cmds));
        buf[0] = CTRL_DATA_STREAM;
        memset(&buf[1], byte, NOKIA_LCD_WIDTH);
        i2c_write(i2c_dev, buf, sizeof(buf), sh1107_addr);
    }
}

int sh1107_hw_init(void)
{
    i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
    if (!device_is_ready(i2c_dev)) {
        printk("display_sh1107: I2C bus not ready\n");
        return -ENODEV;
    }

    printk("display_sh1107: I2C scan...\n");
    int found_count = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (sh1107_probe(addr) == 0) {
            found_count++;
            printk("  found device at 0x%02X\n", addr);
        }
    }
    if (found_count == 0) {
        printk("  no devices found — check wiring\n");
        return -ENOENT;   /* nothing on bus */
    }

    const uint8_t candidates[] = {
        CONFIG_NOKIA_SH1107_I2C_ADDR,
        CONFIG_NOKIA_SH1107_I2C_ADDR ^ 0x01,
    };

    sh1107_addr = 0;
    for (size_t i = 0; i < ARRAY_SIZE(candidates); i++) {
        if (sh1107_probe(candidates[i]) == 0) {
            sh1107_addr = candidates[i];
            printk("display_sh1107: SH1107 found at 0x%02X\n", sh1107_addr);
            break;
        }
    }
    if (sh1107_addr == 0) {
        printk("display_sh1107: no SH1107 at 0x%02X or 0x%02X — check wiring\n",
               CONFIG_NOKIA_SH1107_I2C_ADDR,
               CONFIG_NOKIA_SH1107_I2C_ADDR ^ 0x01);
        return -ENXIO;    /* bus has devices but not SH1107 at expected address */
    }

    static const uint8_t init_seq[] = {
        0xAE,
        0xDC, 0x00,
        0x81, 0x8F,
#if CONFIG_NOKIA_SH1107_SEG_REMAP
        0xA1,
#else
        0xA0,
#endif
#if CONFIG_NOKIA_SH1107_COM_REMAP
        0xC8,
#else
        0xC0,
#endif
        0xA4,
        0xA6,
        0xA8, 0x7F,
        0xD3, 0x00,
        0xD5, 0x51,
        0xD9, 0x22,
        0xDB, 0x35,
        0xAD, 0x8A,
    };
    static const uint8_t display_on[] = { 0xAF };

    int ret = sh1107_cmds(init_seq, sizeof(init_seq));
    if (ret) {
        printk("display_sh1107: init failed (%d)\n", ret);
        return ret;
    }

    k_sleep(K_MSEC(100));

    ret = sh1107_cmds(display_on, sizeof(display_on));
    if (ret) {
        printk("display_sh1107: display-on failed (%d)\n", ret);
        return ret;
    }

    sh1107_fill(0xFF);
    k_sleep(K_MSEC(1000));
    sh1107_fill(0x00);

    printk("display_sh1107: SH1107 ready\n");
    return 0;
}

int sh1107_write_page(int page, const uint8_t *buf)
{
    uint8_t tx[1 + NOKIA_LCD_WIDTH];
    uint8_t addr_cmds[] = { 0xB0 | (uint8_t)page, 0x00, 0x10 };
    int ret;

    if (page < 0 || page >= NOKIA_LCD_PAGES || buf == NULL || sh1107_addr == 0) {
        return -EINVAL;
    }

    ret = sh1107_cmds(addr_cmds, sizeof(addr_cmds));
    if (ret) {
        return ret;
    }

    tx[0] = CTRL_DATA_STREAM;
    memcpy(&tx[1], buf, NOKIA_LCD_WIDTH);
    return i2c_write(i2c_dev, tx, sizeof(tx), sh1107_addr);
}

#endif
