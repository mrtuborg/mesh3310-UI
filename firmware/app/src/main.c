#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void)
{
    printk("Nokia3310 UI starting...\n");

    while (1) {
        printk("tick\n");
        k_sleep(K_SECONDS(1));
    }
}
