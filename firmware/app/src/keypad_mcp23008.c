#include "keypad_mcp23008.h"

#ifdef CONFIG_NOKIA_KEYPAD_MCP23008

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

/* MCP23008 register addresses */
#define MCP_IODIR   0x00   /* I/O direction: 1 = input, 0 = output     */
#define MCP_GPPU    0x06   /* Pull-up resistors: 1 = enabled            */
#define MCP_GPIO    0x09   /* GPIO port register (read)                 */
#define MCP_OLAT    0x0A   /* Output latch (write)                      */

/*
 * GP0–GP3  row outputs  (active-LOW scan)
 * GP4–GP7  column inputs (pull-ups enabled, LOW when button pressed)
 */
#define MCP_IODIR_VALUE  0xF0   /* lower nibble = output, upper = input */
#define MCP_GPPU_VALUE   0xF0   /* enable pull-ups on column inputs      */
#define MCP_ROWS_IDLE    0x0F   /* all row outputs HIGH (idle)           */

/*
 * Matrix layout — matches lcd_viewer.py "4x4_mcp23008" profile:
 *
 *        COL0    COL1    COL2    COL3
 * ROW0 [  '1'    '2'↑    '3'    'A'◄ ]
 * ROW1 [ '4'←   '5'●   '6'→   'B'►  ]
 * ROW2 [  '7'   '8'↓    '9'    'C'   ]
 * ROW3 [  '*'    '0'    '#'    'D'   ]
 *
 * Navigation mapping (matches simulator nav_char_map):
 *   '2'(0,1) → KEY_UP      '8'(2,1) → KEY_DOWN
 *   '4'(1,0) → KEY_LEFT    '6'(1,2) → KEY_RIGHT
 *   '5'(1,1) → KEY_OK
 *   'A'(0,3) → KEY_LEFT    'B'(1,3) → KEY_RIGHT  (softkey shortcuts)
 */
static const nokia_key_t KEY_MAP[4][4] = {
    { KEY_NONE, KEY_UP,    KEY_NONE,  KEY_LEFT  },  /* row 0: 1  2  3  A */
    { KEY_LEFT, KEY_OK,    KEY_RIGHT, KEY_RIGHT },  /* row 1: 4  5  6  B */
    { KEY_NONE, KEY_DOWN,  KEY_NONE,  KEY_NONE  },  /* row 2: 7  8  9  C */
    { KEY_NONE, KEY_NONE,  KEY_NONE,  KEY_NONE  },  /* row 3: *  0  #  D */
};

static const struct device *mcp_i2c;
static uint8_t              mcp_addr;
static uint16_t             prev_pressed; /* bitmask of last-known pressed keys */

static nokia_key_t rotate_key(nokia_key_t k)
{
#if CONFIG_NOKIA_KEYPAD_ROTATION == 90
    switch (k) {
    case KEY_UP:    return KEY_RIGHT;
    case KEY_RIGHT: return KEY_DOWN;
    case KEY_DOWN:  return KEY_LEFT;
    case KEY_LEFT:  return KEY_UP;
    default:        return k;
    }
#elif CONFIG_NOKIA_KEYPAD_ROTATION == 180
    switch (k) {
    case KEY_UP:    return KEY_DOWN;
    case KEY_DOWN:  return KEY_UP;
    case KEY_LEFT:  return KEY_RIGHT;
    case KEY_RIGHT: return KEY_LEFT;
    default:        return k;
    }
#elif CONFIG_NOKIA_KEYPAD_ROTATION == 270
    switch (k) {
    case KEY_UP:    return KEY_LEFT;
    case KEY_LEFT:  return KEY_DOWN;
    case KEY_DOWN:  return KEY_RIGHT;
    case KEY_RIGHT: return KEY_UP;
    default:        return k;
    }
#else
    return k;
#endif
}

static int mcp_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };

    return i2c_write(mcp_i2c, buf, sizeof(buf), mcp_addr);
}

static int mcp_read_gpio(uint8_t *out)
{
    return i2c_write_read(mcp_i2c, mcp_addr, &(uint8_t){MCP_GPIO}, 1, out, 1);
}

int keypad_mcp23008_init(void)
{
    mcp_i2c  = DEVICE_DT_GET(DT_NODELABEL(i2c1));
    mcp_addr = CONFIG_NOKIA_MCP23008_I2C_ADDR;

    if (!device_is_ready(mcp_i2c)) {
        printk("keypad_mcp23008: I2C bus not ready\n");
        return -ENODEV;
    }

    /* Verify the chip responds */
    uint8_t dummy;
    if (i2c_read(mcp_i2c, &dummy, 1, mcp_addr) != 0) {
        printk("keypad_mcp23008: MCP23008 not found at 0x%02X\n", mcp_addr);
        return -ENODEV;
    }

    int ret;

    ret = mcp_write_reg(MCP_IODIR, MCP_IODIR_VALUE);
    if (ret) { printk("keypad_mcp23008: IODIR write failed (%d)\n", ret); return ret; }

    ret = mcp_write_reg(MCP_GPPU, MCP_GPPU_VALUE);
    if (ret) { printk("keypad_mcp23008: GPPU write failed (%d)\n", ret); return ret; }

    ret = mcp_write_reg(MCP_OLAT, MCP_ROWS_IDLE);
    if (ret) { printk("keypad_mcp23008: OLAT write failed (%d)\n", ret); return ret; }

    prev_pressed = 0;
    printk("keypad_mcp23008: MCP23008 ready at 0x%02X\n", mcp_addr);
    return 0;
}

nokia_key_t keypad_mcp23008_poll(void)
{
    /*
     * Optimised scan: drive all rows LOW at once, read columns.
     * If any column is pressed, do per-row scan to identify which key.
     * This reduces I2C traffic from 9 transactions to 2 in the idle case.
     */
    uint8_t gpio_val = 0;

    /* Drive all rows LOW */
    if (mcp_write_reg(MCP_OLAT, 0x00) != 0) {
        return KEY_NONE;
    }

    k_busy_wait(50);

    if (mcp_read_gpio(&gpio_val) != 0) {
        mcp_write_reg(MCP_OLAT, MCP_ROWS_IDLE);
        return KEY_NONE;
    }

    uint8_t any_col = (~gpio_val >> 4) & 0x0F;

    if (any_col == 0) {
        /* No key pressed — restore idle and return early */
        mcp_write_reg(MCP_OLAT, MCP_ROWS_IDLE);
        prev_pressed = 0;
        return KEY_NONE;
    }

    /* At least one key is pressed — do per-row scan */
    uint16_t    now_pressed = 0;
    nokia_key_t first_new   = KEY_NONE;

    for (int r = 0; r < 4; r++) {
        uint8_t olat = MCP_ROWS_IDLE & ~(uint8_t)(1u << r);

        if (mcp_write_reg(MCP_OLAT, olat) != 0) {
            continue;
        }

        k_busy_wait(50);

        gpio_val = 0;
        if (mcp_read_gpio(&gpio_val) != 0) {
            continue;
        }

        uint8_t cols = (~gpio_val >> 4) & 0x0F;

        for (int c = 0; c < 4; c++) {
            if (cols & (1u << c)) {
                uint16_t bit = (uint16_t)(1u << (r * 4 + c));
                now_pressed |= bit;

                if (first_new == KEY_NONE && !(prev_pressed & bit)) {
                    nokia_key_t k = KEY_MAP[r][c];
                    if (k != KEY_NONE) {
                        first_new = k;
                    }
                }
            }
        }
    }

    mcp_write_reg(MCP_OLAT, MCP_ROWS_IDLE);

    prev_pressed = now_pressed;

    first_new = rotate_key(first_new);

    if (first_new != KEY_NONE) {
        static const char * const key_names[] = {
            [KEY_NONE]  = "NONE",
            [KEY_LEFT]  = "LEFT",
            [KEY_RIGHT] = "RIGHT",
            [KEY_UP]    = "UP",
            [KEY_DOWN]  = "DOWN",
            [KEY_OK]    = "OK",
        };
        printk("keypad: %s\n", key_names[first_new]);
    }

    return first_new;
}

#endif /* CONFIG_NOKIA_KEYPAD_MCP23008 */
