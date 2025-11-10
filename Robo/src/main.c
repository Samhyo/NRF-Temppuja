// 1 pisteen suoritus (päivitetty UART yhteensopivaksi Robot Frameworkin kanssa)
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <zephyr/timing/timing.h>

#define THREAD_STACK_SIZE 500
#define THREAD_PRIORITY 5
#define UART_BUFFER_SIZE 20

#define TIME_PARSE_LEN_ERROR -1
#define TIME_PARSE_VALUE_ERROR -3
#define TIME_PARSE_ZERO_ERROR -4
#define TIME_PARSE_NULL_ERROR -5
#define TIME_PARSE_NONDIGIT_ERROR -6

#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)
#define BTN_RED DT_ALIAS(sw0)
#define BTN_YELLOW DT_ALIAS(sw1)
#define BTN_GREEN DT_ALIAS(sw2)
#define BTN_DEBUG DT_ALIAS(sw3)
#define BTN_RESERVED DT_ALIAS(sw4)

static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

static const struct gpio_dt_spec red = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec blue = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

static const struct gpio_dt_spec btn_red = GPIO_DT_SPEC_GET_OR(BTN_RED, gpios, {0});
static const struct gpio_dt_spec btn_yellow = GPIO_DT_SPEC_GET_OR(BTN_YELLOW, gpios, {0});
static const struct gpio_dt_spec btn_green = GPIO_DT_SPEC_GET_OR(BTN_GREEN, gpios, {0});
static const struct gpio_dt_spec btn_debug = GPIO_DT_SPEC_GET_OR(BTN_DEBUG, gpios, {0});
static const struct gpio_dt_spec btn_reserved = GPIO_DT_SPEC_GET_OR(BTN_RESERVED, gpios, {0});

static struct gpio_callback cb_btn_red;
static struct gpio_callback cb_btn_yellow;
static struct gpio_callback cb_btn_green;
static struct gpio_callback cb_btn_debug;
static struct gpio_callback cb_btn_reserved;

K_FIFO_DEFINE(data_fifo);
K_SEM_DEFINE(red_sem, 0, 1);
K_SEM_DEFINE(yellow_sem, 0, 1);
K_SEM_DEFINE(green_sem, 0, 1);
K_SEM_DEFINE(release_sem, 0, 1);
K_SEM_DEFINE(debug_sem, 0, 1);

struct data_t {
    void *fifo_reserved;
    char seq[10];
    int len;
    uint64_t time;
};

// ---------------- TIME PARSER ----------------

int time_parse(const char *time) {
    if (!time) return TIME_PARSE_NULL_ERROR;
    if (strlen(time) != 6) return TIME_PARSE_LEN_ERROR;

    for (int i = 0; i < 6; i++) {
        if (!isdigit((unsigned char)time[i])) return TIME_PARSE_NONDIGIT_ERROR;
    }

    int hh = (time[0] - '0') * 10 + (time[1] - '0');
    int mm = (time[2] - '0') * 10 + (time[3] - '0');
    int ss = (time[4] - '0') * 10 + (time[5] - '0');

    if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) {
        return TIME_PARSE_VALUE_ERROR;
    }

    int total_sec = hh * 3600 + mm * 60 + ss;
    if (total_sec == 0) return TIME_PARSE_ZERO_ERROR;

    return total_sec;
}

// ---------------- INIT FUNCTIONS ----------------

int init_uart(void) {
    if (!device_is_ready(uart_dev)) {
        return 1;
    }
    return 0;
}

int init_leds(void) {
    int r = gpio_pin_configure_dt(&red, GPIO_OUTPUT_ACTIVE);
    r |= gpio_pin_configure_dt(&green, GPIO_OUTPUT_ACTIVE);
    r |= gpio_pin_configure_dt(&blue, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set_dt(&red, 0);
    gpio_pin_set_dt(&green, 0);
    gpio_pin_set_dt(&blue, 0);
    return r;
}

// ---------------- BUTTON HANDLERS ----------------

void button_add_char(char c) {
    struct data_t *item = k_malloc(sizeof(struct data_t));
    if (item) {
        item->seq[0] = c;
        item->len = 1;
        item->time = k_uptime_get();
        k_fifo_put(&data_fifo, item);
    }
}

void btn_red_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    button_add_char('R');
}

void btn_yellow_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    button_add_char('Y');
}

void btn_green_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    button_add_char('G');
}

void btn_debug_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    button_add_char('D');
}

void btn_reserved_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    button_add_char('F');
}

int init_buttons(void) {
    const struct gpio_dt_spec buttons[] = {btn_red, btn_yellow, btn_green, btn_debug, btn_reserved};
    struct gpio_callback *callbacks[] = {&cb_btn_red, &cb_btn_yellow, &cb_btn_green, &cb_btn_debug, &cb_btn_reserved};
    void (*handlers[])(const struct device *, struct gpio_callback *, uint32_t) = {
        btn_red_handler, btn_yellow_handler, btn_green_handler, btn_debug_handler, btn_reserved_handler
    };

    for (int i = 0; i < 5; i++) {
        if (!gpio_is_ready_dt(&buttons[i])) {
            printk("Button %d not ready\n", i);
            return -1;
        }
        if (gpio_pin_configure_dt(&buttons[i], GPIO_INPUT) != 0) {
            printk("Button %d config failed\n", i);
            return -1;
        }
        if (gpio_pin_interrupt_configure_dt(&buttons[i], GPIO_INT_EDGE_TO_ACTIVE) != 0) {
            printk("Button %d interrupt failed\n", i);
            return -1;
        }
        gpio_init_callback(callbacks[i], handlers[i], BIT(buttons[i].pin));
        gpio_add_callback(buttons[i].port, callbacks[i]);
    }
    printk("All buttons initialized\n");
    return 0;
}

// ---------------- THREAD TASKS ----------------

void led_red_task(void *, void *, void *) {
    while (true) {
        k_sem_take(&red_sem, K_FOREVER);
        gpio_pin_set_dt(&red, 1);
        k_sleep(K_MSEC(1000));
        gpio_pin_set_dt(&red, 0);
        k_sem_give(&release_sem);
        k_yield();
        printk("Red led task running\n");
    }
}

void led_yellow_task(void *, void *, void *) {
    while (true) {
        k_sem_take(&yellow_sem, K_FOREVER);
        gpio_pin_set_dt(&red, 1);
        gpio_pin_set_dt(&green, 1);
        k_sleep(K_MSEC(1000));
        gpio_pin_set_dt(&red, 0);
        gpio_pin_set_dt(&green, 0);
        k_sem_give(&release_sem);
    }
}

void led_green_task(void *, void *, void *) {
    while (true) {
        k_sem_take(&green_sem, K_FOREVER);
        gpio_pin_set_dt(&green, 1);
        k_sleep(K_MSEC(1000));
        gpio_pin_set_dt(&green, 0);
        k_sem_give(&release_sem);
    }
}

// ---------------- UART TASK (Päivitetty) ----------------

void uart_task(void *, void *, void *) {
    char rc = 0;
    char uart_msg[UART_BUFFER_SIZE];
    int uart_msg_cnt = 0;

    while (true) {
        if (uart_poll_in(uart_dev, &rc) == 0) {
            if (rc == '\r' || rc == '\n') {
                if (uart_msg_cnt > 0) {
                    uart_msg[uart_msg_cnt] = '\0';
                    int ret = time_parse(uart_msg);
                    printk("%d\n", ret);  // Robot Framework lukee tämän rivin
                    uart_msg_cnt = 0;
                    memset(uart_msg, 0, sizeof(uart_msg));
                }
            } else {
                if (uart_msg_cnt < UART_BUFFER_SIZE - 1) {
                    uart_msg[uart_msg_cnt++] = rc;
                }
            }
        }
        k_msleep(10);
    }
}

void dispatcher_task(void *, void *, void *) {
    while (true) {
        struct data_t *rec_item = k_fifo_get(&data_fifo, K_FOREVER);

        for (int i = 0; i < rec_item->len; i++) {
            char c = rec_item->seq[i];

            if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';

            switch (c) {
                case 'R':
                    k_sem_give(&red_sem);
                    break;
                case 'Y':
                    k_sem_give(&yellow_sem);
                    break;
                case 'G':
                    k_sem_give(&green_sem);
                    break;
                case 'D':
                    k_sem_give(&debug_sem);
                    break;
                default:
                    printk("Was given wrong char, give a new one\n");
                    break;
            }
            k_sem_take(&release_sem, K_FOREVER);
        }

        k_free(rec_item);
    }
}

void debug_task(void *, void *, void *) {
    while (true) {
        k_sem_take(&debug_sem, K_FOREVER);

        struct data_t *received = k_fifo_get(&data_fifo, K_FOREVER);
        if (received) {
            printk("Debug received: %lld\n", received->time);
            k_free(received);
        }

        k_sem_give(&release_sem);
        k_yield();
    }
}

K_THREAD_DEFINE(red_thread, THREAD_STACK_SIZE, led_red_task, NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
K_THREAD_DEFINE(yellow_thread, THREAD_STACK_SIZE, led_yellow_task, NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
K_THREAD_DEFINE(green_thread, THREAD_STACK_SIZE, led_green_task, NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
K_THREAD_DEFINE(uart_thread, THREAD_STACK_SIZE, uart_task, NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
K_THREAD_DEFINE(dispatcher_thread, THREAD_STACK_SIZE, dispatcher_task, NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
K_THREAD_DEFINE(debug_thread, THREAD_STACK_SIZE, debug_task, NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);

// ---------------- MAIN ----------------

int main(void) {
    if (init_uart() != 0) {
        printk("UART initialization failed\n");
        return 1;
    }

    timing_init();
    timing_start();
    timing_t start_time = timing_counter_get();

    k_msleep(100);
    init_leds();
    init_buttons();

    printk("Program started..\n");

    timing_t end_time = timing_counter_get();
    timing_stop();
    uint64_t timing_ns = timing_cycles_to_ns(timing_cycles_get(&start_time, &end_time));
    printk("Initialization: %lld ns\n", timing_ns);

    while (1) k_sleep(K_MSEC(100));

    return 0;
}
