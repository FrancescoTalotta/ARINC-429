#include "arinc_rx.h"
#include "arinc_rx_decode.h"
#include "arinc_rx_filter.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_struct.h"

#define RX_CLOCK_GPIO 11
#define RX_DATA_GPIO 12
#define RX_QUEUE_CAPACITY 256U
#define RX_TASK_BUDGET 64U
#define RX_DATAGRAM_BYTES 1200U

typedef struct {
    uint64_t count;
    uint64_t dropped;
    uint32_t raw;
} rx_capture_t;

/* Fixed DRAM ring queue: no allocation, RTOS queue calls, logging, decoding or
 * flash-resident gpio_get_level() in the ISR. All shared 64-bit values and
 * framing state are protected by the same short cross-core spinlock. */
static DRAM_ATTR rx_capture_t s_queue[RX_QUEUE_CAPACITY];
static DRAM_ATTR portMUX_TYPE s_rx_lock = portMUX_INITIALIZER_UNLOCKED;
static DRAM_ATTR unsigned s_head, s_tail, s_used;
static DRAM_ATTR uint64_t s_count, s_dropped;
static DRAM_ATTR uint32_t s_acc, s_nbits;
static DRAM_ATTR int64_t s_last_edge_us;
static DRAM_ATTR uint32_t s_bit_us = 10;
static DRAM_ATTR bool s_receiving;

/* Separate lock: network snapshots never hold the ISR's framing lock. */
static portMUX_TYPE s_destination_lock = portMUX_INITIALIZER_UNLOCKED;
static struct sockaddr_in s_destination;
static uint32_t s_destination_generation;
static bool s_initialized;
static const char *TAG = "ARINC_RX";

static void IRAM_ATTR arinc_rx_clock_isr(void *arg)
{
    (void)arg;
    /* Sample before lock acquisition so task-side contention cannot shift
     * the sample across a data transition. GPIO11/12 are in the low bank. */
    uint32_t bit = (GPIO.in >> RX_DATA_GPIO) & 1U;
    portENTER_CRITICAL_ISR(&s_rx_lock);
    int64_t now = esp_timer_get_time();
    int64_t dt = now - s_last_edge_us;
    s_last_edge_us = now;

    /* Rising-edge distance: normal bits are 10/80 us, a legal interword gap
     * is >=4 bit times (40/320 us). Start only after observed idle, including
     * boot and speed changes. Never free-run another 32 bits without a gap.
     * An early gap abandons a partial word. Out-of-window intra-word edges
     * (including a missed or extra clock) abandon framing until another gap. */
    if (dt >= 4U * s_bit_us) {
        s_acc = 0;
        s_nbits = 0;
        s_receiving = true;
    } else if (dt < s_bit_us / 2U || dt > s_bit_us + s_bit_us / 2U) {
        s_receiving = false;
    }

    if (s_receiving) {
        s_acc = (s_acc << 1) | bit;
        if (++s_nbits == 32) {
            ++s_count;
            s_receiving = false;
            if (s_used == RX_QUEUE_CAPACITY) {
                ++s_dropped;
            } else {
                rx_capture_t *item = &s_queue[s_head];
                item->count = s_count;
                item->dropped = s_dropped;
                item->raw = s_acc;
                s_head = (s_head + 1U) & (RX_QUEUE_CAPACITY - 1U);
                ++s_used;
            }
        }
    }
    portEXIT_CRITICAL_ISR(&s_rx_lock);
}

void arinc_rx_set_high_speed(bool high_speed)
{
    portENTER_CRITICAL(&s_rx_lock);
    s_bit_us = high_speed ? 10 : 80;
    s_receiving = false;
    s_acc = 0;
    s_nbits = 0;
    s_last_edge_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s_rx_lock);
}

void arinc_rx_subscribe(uint32_t ipv4, uint16_t port)
{
    struct sockaddr_in destination = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = ipv4,
    };
    portENTER_CRITICAL(&s_destination_lock);
    s_destination = destination;
    ++s_destination_generation;
    portEXIT_CRITICAL(&s_destination_lock);
}

static void rx_send_batch(int sock, const struct sockaddr_in *to, const char *data, size_t len)
{
    if (!len) {
        return;
    }
    /* Socket is nonblocking. Network losses are intentionally NOT included
     * in the protocol's dropped counter, which counts only ISR queue losses. */
    if (sendto(sock, data, len, 0, (const struct sockaddr *)to, sizeof(*to)) < 0) {
        static int64_t last_warning_us;
        int64_t now = esp_timer_get_time();
        if (now - last_warning_us >= 1000000) {
            ESP_LOGW(TAG, "RX UDP batch lost, errno=%d", errno);
            last_warning_us = now;
        }
    }
}

static void arinc_rx_task(void *arg)
{
    (void)arg;
    int sock = -1;
    int64_t retry_us = 0;
    char batch[RX_DATAGRAM_BYTES];
    static arinc_rx_filter_t filter;
    uint32_t filter_generation = 0;
    uint64_t report_count = 0;

    for (;;) {
        struct sockaddr_in destination;
        uint32_t destination_generation;
        portENTER_CRITICAL(&s_destination_lock);
        destination = s_destination;
        destination_generation = s_destination_generation;
        portEXIT_CRITICAL(&s_destination_lock);

        if (destination_generation != filter_generation) {
            arinc_rx_filter_reset(&filter);
            filter_generation = destination_generation;
        }

        /* Subscription can only be established by the initialized UDP control
         * task. Until then, do not touch lwIP: RX starts before esp_netif_init. */
        if (destination.sin_port && sock < 0 && esp_timer_get_time() >= retry_us) {
            sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
            if (sock >= 0 && fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
                close(sock);
                sock = -1;
            }
            retry_us = esp_timer_get_time() + 1000000;
            if (sock < 0) {
                ESP_LOGW(TAG, "RX UDP socket unavailable, errno=%d", errno);
            }
        }

        size_t used = 0;
        for (unsigned work = 0; work < RX_TASK_BUDGET; ++work) {
            rx_capture_t item;
            portENTER_CRITICAL(&s_rx_lock);
            bool available = s_used != 0;
            if (available) {
                item = s_queue[s_tail];
                s_tail = (s_tail + 1U) & (RX_QUEUE_CAPACITY - 1U);
                --s_used;
            }
            portEXIT_CRITICAL(&s_rx_lock);
            if (!available) {
                break;
            }
            if (!destination.sin_port || sock < 0) {
                continue;
            }

            arinc_rx_decoded_t decoded = arinc_rx_decode(item.raw);
            if (!arinc_rx_filter_changed(&filter, &decoded)) {
                continue;
            }
            ++report_count;
            char line[192];
            int len = snprintf(line, sizeof(line),
                "rx count=%" PRIu64 " raw=0x%08" PRIX32
                " label=%03o sdi=%u data=0x%05" PRIX32 " ssm=%u parity=%u dropped=%" PRIu64 "\n",
                report_count, item.raw, (unsigned)decoded.label,
                (unsigned)decoded.sdi, decoded.data19, (unsigned)decoded.ssm,
                decoded.parity_ok ? 1U : 0U, item.dropped);
            if (len <= 0 || len >= (int)sizeof(line)) {
                continue;
            }
            if (used + (size_t)len > sizeof(batch)) {
                rx_send_batch(sock, &destination, batch, used);
                used = 0;
            }
            memcpy(batch + used, line, (size_t)len);
            used += (size_t)len;
        }
        rx_send_batch(sock, &destination, batch, used);
        /* Bounded drain, even during continuous traffic or without a subscriber.
         * One tick sleep also guarantees idle/TX tasks can run. */
        vTaskDelay(1);
    }
}

esp_err_t arinc_rx_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << RX_CLOCK_GPIO) | (1ULL << RX_DATA_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    /* Do not accept an unknown pre-existing service: its IRAM allocation flags
     * cannot be checked. app_main calls us before W5500 initialization. */
    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_isr_handler_add(RX_CLOCK_GPIO, arinc_rx_clock_isr, NULL);
    if (err != ESP_OK) {
        gpio_uninstall_isr_service();
        return err;
    }
    arinc_rx_set_high_speed(true);
    err = gpio_set_intr_type(RX_CLOCK_GPIO, GPIO_INTR_POSEDGE);
    if (err == ESP_OK) {
        err = gpio_intr_enable(RX_CLOCK_GPIO);
    }
    if (err != ESP_OK) {
        gpio_isr_handler_remove(RX_CLOCK_GPIO);
        gpio_uninstall_isr_service();
        return err;
    }
    if (xTaskCreate(arinc_rx_task, "arinc_rx", 4096, NULL, 3, NULL) != pdPASS) {
        gpio_intr_disable(RX_CLOCK_GPIO);
        gpio_isr_handler_remove(RX_CLOCK_GPIO);
        gpio_uninstall_isr_service();
        return ESP_ERR_NO_MEM;
    }
    s_initialized = true;
    ESP_LOGI(TAG, "Passive RX clock=11 data=12, high speed, subscription disabled");
    return ESP_OK;
}
