#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include <stdlib.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <assert.h>

#define STACKSIZE 500
#define PRIORITY 5

#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)
static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

static const struct gpio_dt_spec red   = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec blue  = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

K_FIFO_DEFINE(dispatcher_fifo);
K_FIFO_DEFINE(debug_fifo);

struct data_t {
    void *fifo_reserved;
    char msg[80];
};

K_MUTEX_DEFINE(red_mutex);
K_CONDVAR_DEFINE(red_cv);
K_MUTEX_DEFINE(green_mutex);
K_CONDVAR_DEFINE(green_cv);
K_MUTEX_DEFINE(yellow_mutex);
K_CONDVAR_DEFINE(yellow_cv);
K_SEM_DEFINE(release_sem, 0, 1);

bool debug_on = true;

void debug_print(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[128];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    struct data_t *msg = k_malloc(sizeof(struct data_t));
    assert(msg != NULL);
    strncpy(msg->msg, buf, sizeof(msg->msg));
    k_fifo_put(&debug_fifo, msg);
}

void set_leds(bool r, bool g, bool b) {
    gpio_pin_set_dt(&red, r);
    gpio_pin_set_dt(&green, g);
    gpio_pin_set_dt(&blue, b);
}

int init_gpio(void) {
    gpio_pin_configure_dt(&red, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&green, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&blue, GPIO_OUTPUT_INACTIVE);
    set_leds(0, 0, 0);
    return 0;
}

static void uart_task(void *unused1, void *unused2, void *unused3) {
    char rc = 0;
    char uart_msg[80];
    memset(uart_msg, 0, sizeof(uart_msg));
    int uart_msg_cnt = 0;
    while (true) {
        if (uart_poll_in(uart_dev, &rc) == 0) {
            if (rc == 'D' || rc == 'd') {
                debug_on = !debug_on;
                continue;
            }
            if (rc != '\r' && rc != '\n') {
                if (uart_msg_cnt < sizeof(uart_msg) - 1) {
                    uart_msg[uart_msg_cnt] = rc;
                    uart_msg_cnt++;
                }
            } else if (uart_msg_cnt > 0) {
                struct data_t *buf = k_malloc(sizeof(struct data_t));
                assert(buf != NULL);
                strncpy(buf->msg, uart_msg, sizeof(buf->msg));
                k_fifo_put(&dispatcher_fifo, buf);
                uart_msg_cnt = 0;
                memset(uart_msg, 0, sizeof(uart_msg));
            }
        }
        k_msleep(10);
    }
}

static void red_task(void *unused1, void *unused2, void *unused3) {
    while (1) {
        k_mutex_lock(&red_mutex, K_FOREVER);
        k_condvar_wait(&red_cv, &red_mutex, K_FOREVER);
        int *duration = (int *)unused1;
        assert(*duration > 0);
        uint64_t start = k_cycle_get_64();
        set_leds(1, 0, 0);
        k_msleep(*duration);
        set_leds(0, 0, 0);
        uint64_t end = k_cycle_get_64();
        debug_print("RED task duration: %lld us", k_cyc_to_us_near64(end - start));
        k_sem_give(&release_sem);
        k_mutex_unlock(&red_mutex);
    }
}

static void green_task(void *unused1, void *unused2, void *unused3) {
    while (1) {
        k_mutex_lock(&green_mutex, K_FOREVER);
        k_condvar_wait(&green_cv, &green_mutex, K_FOREVER);
        int *duration = (int *)unused1;
        assert(*duration > 0);
        uint64_t start = k_cycle_get_64();
        set_leds(0, 1, 0);
        k_msleep(*duration);
        set_leds(0, 0, 0);
        uint64_t end = k_cycle_get_64();
        debug_print("GREEN task duration: %lld us", k_cyc_to_us_near64(end - start));
        k_sem_give(&release_sem);
        k_mutex_unlock(&green_mutex);
    }
}

static void yellow_task(void *unused1, void *unused2, void *unused3) {
    while (1) {
        k_mutex_lock(&yellow_mutex, K_FOREVER);
        k_condvar_wait(&yellow_cv, &yellow_mutex, K_FOREVER);
        int *duration = (int *)unused1;
        assert(*duration > 0);
        uint64_t start = k_cycle_get_64();
        set_leds(1, 1, 0);
        k_msleep(*duration);
        set_leds(0, 0, 0);
        uint64_t end = k_cycle_get_64();
        debug_print("YELLOW task duration: %lld us", k_cyc_to_us_near64(end - start));
        k_sem_give(&release_sem);
        k_mutex_unlock(&yellow_mutex);
    }
}

static void dispatcher_task(void *unused1, void *unused2, void *unused3) {
    while (true) {
        struct data_t *rec_item = k_fifo_get(&dispatcher_fifo, K_FOREVER);
        assert(rec_item != NULL);
        char sequence[80];
        strncpy(sequence, rec_item->msg, sizeof(sequence));
        k_free(rec_item);
        uint64_t seq_start = k_cycle_get_64();
        char *token = strtok(sequence, " ");
        while (token != NULL) {
            char color = token[0];
            int time = 1000;
            for (int i = 1; i < strlen(token); i++) {
                if (token[i] == ',') {
                    time = atoi(token + i + 1);
                    break;
                }
            }
            static int duration;
            duration = time;
            debug_print("Dispatcher: Color %c for %d ms", color, time);
            if (color == 'r' || color == 'R') {
                k_mutex_lock(&red_mutex, K_FOREVER);
                k_condvar_signal(&red_cv);
                k_mutex_unlock(&red_mutex);
            } else if (color == 'g' || color == 'G') {
                k_mutex_lock(&green_mutex, K_FOREVER);
                k_condvar_signal(&green_cv);
                k_mutex_unlock(&green_mutex);
            } else if (color == 'y' || color == 'Y') {
                k_mutex_lock(&yellow_mutex, K_FOREVER);
                k_condvar_signal(&yellow_cv);
                k_mutex_unlock(&yellow_mutex);
            } else {
                assert(0 && "Invalid color in sequence");
            }
            k_sem_take(&release_sem, K_FOREVER);
            token = strtok(NULL, " ");
        }
        uint64_t seq_end = k_cycle_get_64();
        debug_print("Sequence total duration: %lld us", k_cyc_to_us_near64(seq_end - seq_start));
    }
}

static void debug_task(void *unused1, void *unused2, void *unused3) {
    while (1) {
        struct data_t *msg = k_fifo_get(&debug_fifo, K_FOREVER);
        assert(msg != NULL);
        if (debug_on) {
            printk("%s\n", msg->msg);
        }
        k_free(msg);
    }
}

int main(void) {
    if (!device_is_ready(uart_dev)) return -1;
    init_gpio();
    printk("Traffic Light Controller Ready\n");
    printk("Send e.g.: R,1000 Y,500 G,2000 or D to toggle debug\n");
    static int dummy_duration = 1000;
    k_thread_create(K_THREAD_STACK_DEFINE(red_stack, STACKSIZE), red_stack, STACKSIZE, red_task, &dummy_duration, NULL, NULL, PRIORITY, 0, K_NO_WAIT);
    k_thread_create(K_THREAD_STACK_DEFINE(green_stack, STACKSIZE), green_stack, STACKSIZE, green_task, &dummy_duration, NULL, NULL, PRIORITY, 0, K_NO_WAIT);
    k_thread_create(K_THREAD_STACK_DEFINE(yellow_stack, STACKSIZE), yellow_stack, STACKSIZE, yellow_task, &dummy_duration, NULL, NULL, PRIORITY, 0, K_NO_WAIT);
    k_thread_create(K_THREAD_STACK_DEFINE(dispatcher_stack, STACKSIZE), dispatcher_stack, STACKSIZE, dispatcher_task, NULL, NULL, NULL, PRIORITY, 0, K_NO_WAIT);
    k_thread_create(K_THREAD_STACK_DEFINE(uart_stack, STACKSIZE), uart_stack, STACKSIZE, uart_task, NULL, NULL, NULL, PRIORITY, 0, K_NO_WAIT);
    k_thread_create(K_THREAD_STACK_DEFINE(debug_stack, STACKSIZE), debug_stack, STACKSIZE, debug_task, NULL, NULL, NULL, PRIORITY - 1, 0, K_NO_WAIT);
    return 0;
}
