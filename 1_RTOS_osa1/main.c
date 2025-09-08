#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

static const struct gpio_dt_spec red   = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec blue  = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);
#define BUTTON_0 DT_ALIAS(sw0)
static const struct gpio_dt_spec button_0 = GPIO_DT_SPEC_GET_OR(BUTTON_0, gpios, {0});

volatile int led_state = 0;

void set_leds(bool r, bool g, bool b) {
    gpio_pin_set_dt(&red, r);
    gpio_pin_set_dt(&green, g);
    gpio_pin_set_dt(&blue, b);
}

bool is_button_pressed() {
    return gpio_pin_get_dt(&button_0) == 1;
}

void init_hw(void) {
    gpio_pin_configure_dt(&red,   GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&green, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&blue,  GPIO_OUTPUT_ACTIVE);
    set_leds(0, 0, 0);
    if (!gpio_is_ready_dt(&button_0)) return;
    gpio_pin_configure_dt(&button_0, GPIO_INPUT);
}

void led_task(void *, void *, void *) {
    while (1) {
        if (led_state == 0) {
            set_leds(1, 0, 0);
            printk("Punainen päällä\n");
        } else if (led_state == 1) {
            set_leds(1, 1, 0);
            printk("Keltainen päällä\n");
        } else if (led_state == 2) {
            set_leds(0, 1, 0);
            printk("Vihreä päällä\n");
        }
        int elapsed = 0;
        while (elapsed < 2000) {
            if (is_button_pressed()) {
                while (is_button_pressed()) {
                    k_msleep(10);
                }
            }
            k_msleep(10);
            elapsed += 10;
        }
        set_leds(0, 0, 0);
        led_state = (led_state + 1) % 3;
        k_msleep(10);
    }
}

#define STACKSIZE 500
#define PRIORITY 5
K_THREAD_DEFINE(led_tid, STACKSIZE, led_task, NULL, NULL, NULL, PRIORITY, 0, 0);

int main(void) {
    init_hw();
    while (1) {
        k_msleep(1000);
    }
}