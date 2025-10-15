// tehtävästä luulisi saavan tällä hetkellä ainakin yhden pisteen. kahden ja kolmen pisteen osiot ovan työn alla.

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>

#define STACKSIZE 500
#define PRIORITY 5

#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)
static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

static const struct gpio_dt_spec red   = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec blue  = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

K_FIFO_DEFINE(dispatcher_fifo);

struct data_t {
    void *fifo_reserved;
    char msg[20];
};

void set_leds(bool r, bool g, bool b) {
    gpio_pin_set_dt(&red, r);
    gpio_pin_set_dt(&green, g);
    gpio_pin_set_dt(&blue, b);
}

int init_uart(void) {
    if (!device_is_ready(uart_dev)) {
        return 1;
    }
    return 0;
}

int init_gpio(void) {
    gpio_pin_configure_dt(&red, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&green, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&blue, GPIO_OUTPUT_ACTIVE);
    set_leds(0, 0, 0);
    return 0;
}

int main(void) {
    int ret = init_uart();
    if (ret != 0) {
        printk("UART initialization failed!\n");
        return ret;
    }

    init_gpio();
    printk("LED Controller Started\n");
    printk("Commands: r,1000  g,2000  y,500\n");

    return 0;
}

static void uart_task(void *unused1, void *unused2, void *unused3) {
    char rc = 0;
    char uart_msg[20];
    memset(uart_msg, 0, 20);
    int uart_msg_cnt = 0;

    while (true) {
        if (uart_poll_in(uart_dev, &rc) == 0) {
            if (rc != '\r' && rc != '\n') {
                if (uart_msg_cnt < 19) {
                    uart_msg[uart_msg_cnt] = rc;
                    uart_msg_cnt++;
                }
            } else if (uart_msg_cnt > 0) {
                struct data_t *buf = k_malloc(sizeof(struct data_t));
                if (buf == NULL) {
                    return;
                }
                
                snprintf(buf->msg, 20, "%s", uart_msg);
                k_fifo_put(&dispatcher_fifo, buf);

                uart_msg_cnt = 0;
                memset(uart_msg, 0, 20);
            }
        }
        k_msleep(10);
    }
}

static void dispatcher_task(void *unused1, void *unused2, void *unused3) {
    while (true) {
        struct data_t *rec_item = k_fifo_get(&dispatcher_fifo, K_FOREVER);
        char sequence[20];
        memcpy(sequence, rec_item->msg, 20);
        k_free(rec_item);

        char color = sequence[0];
        int time = 1000;
        
        for (int i = 1; i < strlen(sequence); i++) {
            if (sequence[i] == ',') {
                time = atoi(sequence + i + 1);
                break;
            }
        }

        if (color == 'r' || color == 'R') {
            set_leds(1, 0, 0);
            k_msleep(time);
            set_leds(0, 0, 0);
        } else if (color == 'g' || color == 'G') {
            set_leds(0, 1, 0);
            k_msleep(time);
            set_leds(0, 0, 0);
        } else if (color == 'y' || color == 'Y') {
            set_leds(1, 1, 0);
            k_msleep(time);
            set_leds(0, 0, 0);
        }
    }
}

K_THREAD_DEFINE(dis_thread, STACKSIZE, dispatcher_task, NULL, NULL, NULL, PRIORITY, 0, 0);

K_THREAD_DEFINE(uart_thread, STACKSIZE, uart_task, NULL, NULL, NULL, PRIORITY, 0, 0);
