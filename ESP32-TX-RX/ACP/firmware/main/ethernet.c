#include "ethernet.h"

#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_mac.h"

#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_eth_netif_glue.h"

#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"

#include "esp_ota_ops.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "arinc.h"

static const char *TAG = "ETH";
static const char *TAG_RX = "UDP_RX";
static const char *TAG_OTA_CTRL = "OTA_CTRL";
static const char *TAG_OTA_DATA = "OTA_DATA";

/* ------------------ ETHERNET ------------------ */
#define PIN_MISO  1
#define PIN_RST   2
#define PIN_INT   40
#define PIN_CS    39
#define PIN_SCK   38
#define PIN_MOSI  4

static EventGroupHandle_t s_evt;
static const int GOT_IP_BIT = BIT0;

static esp_netif_t     *s_eth_netif = NULL;
static esp_eth_handle_t s_eth_handle = NULL;

/* ======================= OTA (no crypto, no integrity) ======================= */
typedef struct {
    bool active;
    uint32_t image_id;
    uint32_t expected_size;
    uint16_t chunk_size;

    uint32_t written;
    uint32_t next_seq;

    esp_ota_handle_t handle;
    const esp_partition_t *part;
} ota_session_t;

static ota_session_t g_ota;
static SemaphoreHandle_t g_ota_lock;

#ifndef OTA_CTRL_PORT
#define OTA_CTRL_PORT 9001
#endif

#ifndef OTA_DATA_PORT
#define OTA_DATA_PORT 9002
#endif

#ifndef OTA_MAX_PAYLOAD
#define OTA_MAX_PAYLOAD 1400
#endif

#define OTA_MAGIC 0x4F544131u   // "OTA1"

typedef enum {
    OTA_MSG_START = 1,
    OTA_MSG_DATA  = 2,
    OTA_MSG_ACK   = 3,
    OTA_MSG_NACK  = 4,
    OTA_MSG_DONE  = 5,
    OTA_MSG_ABORT = 6,
} ota_msg_type_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  type;
    uint8_t  rsv0;
    uint16_t rsv1;
    uint32_t image_id;
} ota_hdr_t;

typedef struct __attribute__((packed)) {
    ota_hdr_t h;
    uint32_t image_size;
    uint16_t chunk_size;
    uint16_t rsv;
} ota_start_t;

typedef struct __attribute__((packed)) {
    ota_hdr_t h;
    uint32_t seq;
    uint16_t len;
    uint16_t rsv;
    uint8_t  data[OTA_MAX_PAYLOAD];
} ota_data_t;

typedef struct __attribute__((packed)) {
    ota_hdr_t h;
    uint32_t seq;
} ota_ack_t;

/* =================== ETHERNET ====================== */
static void w5500_hw_reset(void)
{
    gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&rst_cfg));

    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
}

static void eth_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;

    switch (id) {
        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "ETH event: START");
            break;
        case ETHERNET_EVENT_STOP:
            ESP_LOGI(TAG, "ETH event: STOP");
            xEventGroupClearBits(s_evt, GOT_IP_BIT);
            break;
        case ETHERNET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "ETH event: CONNECTED (link up)");
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "ETH event: DISCONNECTED (link down)");
            xEventGroupClearBits(s_evt, GOT_IP_BIT);
            break;
        default:
            ESP_LOGI(TAG, "ETH event: id=%" PRIi32, id);
            break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    ESP_LOGI(TAG, "IP event: id=%" PRIi32, id);

    if (id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "DHCP got IP: " IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&e->ip_info.netmask));
        ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&e->ip_info.gw));
        xEventGroupSetBits(s_evt, GOT_IP_BIT);
    }
}

void ethernet_w5500_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_evt = xEventGroupCreate();

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&cfg);

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL));

    gpio_config_t int_cfg = {
        .pin_bit_mask = 1ULL << PIN_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&int_cfg));

    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_MISO,
        .mosi_io_num = PIN_MOSI,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    w5500_hw_reset();

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    spi_device_interface_config_t spi_devcfg = {
        .mode = 0,
        .clock_speed_hz = 10 * 1000 * 1000,
        .spics_io_num = PIN_CS,
        .queue_size = 20,
    };

    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &spi_devcfg);
    w5500_config.int_gpio_num = PIN_INT;

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &s_eth_handle));

    uint8_t mac_addr[6] = {0};
    ESP_ERROR_CHECK(esp_read_mac(mac_addr, ESP_MAC_WIFI_STA));
    mac_addr[0] |= 0x02;
    mac_addr[0] &= 0xFE;
    mac_addr[5] ^= 0x01;

    ESP_ERROR_CHECK(esp_eth_ioctl(s_eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr));
    ESP_LOGI(TAG, "Set ETH MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

    ESP_ERROR_CHECK(esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(s_eth_handle)));
    ESP_ERROR_CHECK(esp_netif_dhcpc_start(s_eth_netif));
    ESP_ERROR_CHECK(esp_eth_start(s_eth_handle));
}

void ethernet_wait_for_ip(void)
{
    xEventGroupWaitBits(s_evt, GOT_IP_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}

QueueHandle_t ethernet_udp_rx_queue_create(void)
{
    return xQueueCreate(UDP_RX_QUEUE_LEN, sizeof(udp_rx_msg_t));
}

QueueHandle_t ethernet_udp_bits_queue_create(void)
{
    return xQueueCreate(UDP_BITS_QUEUE_LEN, sizeof(uint8_t));
}

#define UDP_BATCH_WORDS 1
#define UDP_CMD_MAGIC 0xDEADBEEFu

void udp_tx_task(void *arg)
{
    QueueHandle_t q = (QueueHandle_t)arg;
    if (!q) vTaskDelete(NULL);

    ethernet_wait_for_ip();
    vTaskDelay(pdMS_TO_TICKS(1000));

    arinc_msg_t stale;
    int dropped = 0;
    while (xQueueReceive(q, &stale, 0) == pdTRUE) {
        dropped++;
    }
    if (dropped > 0) {
        ESP_LOGI(TAG, "Dropped %d startup UDP words", dropped);
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) vTaskDelete(NULL);

    struct sockaddr_in pc = {0};
    pc.sin_family = AF_INET;
    pc.sin_port   = htons(UDP_PC_PORT);
    inet_pton(AF_INET, UDP_PC_IP, &pc.sin_addr);

    uint32_t buf[UDP_BATCH_WORDS];

    while (1) {
        arinc_msg_t item;

        xQueueReceive(q, &item, portMAX_DELAY);
        buf[0] = htonl(item.word);

        int n = 1;
        while (n < UDP_BATCH_WORDS && xQueueReceive(q, &item, 0) == pdTRUE) {
            buf[n++] = htonl(item.word);
        }

        int ret = sendto(sock, buf, n * sizeof(uint32_t), 0,
                         (struct sockaddr *)&pc, sizeof(pc));

        if (ret < 0) {
            ESP_LOGW(TAG, "sendto failed: errno=%d words=%d", errno, n);
            vTaskDelay(pdMS_TO_TICKS(20));
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void udp_rx_task(void *arg)
{
    udp_rx_task_cfg_t *cfg = (udp_rx_task_cfg_t *)arg;
    QueueHandle_t msg_q = cfg ? cfg->msg_queue : NULL;
    QueueHandle_t bits_q = cfg ? cfg->bits_queue : NULL;

    ethernet_wait_for_ip();

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) { ESP_LOGE(TAG_RX, "socket failed errno=%d", errno); vTaskDelete(NULL); return; }

    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_port = htons(UDP_RX_PORT);
    local.sin_addr.s_addr = htonl(INADDR_ANY);

    ESP_LOGI(TAG_RX, "Binding to 0.0.0.0:%d", UDP_RX_PORT);
    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
        ESP_LOGE(TAG_RX, "bind failed errno=%d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG_RX, "Bound OK, waiting...");

    uint8_t buf[UDP_RX_MAX_PAYLOAD];

    while (1) {
        struct sockaddr_in from = {0};
        socklen_t fromlen = sizeof(from);
        int r = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen);
        if (r < 0) {
            ESP_LOGW(TAG_RX, "recvfrom failed errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (msg_q) {
            udp_rx_msg_t msg = {0};
            msg.len = (uint16_t)r;
            msg.from_port = ntohs(from.sin_port);
            msg.from_ip = from.sin_addr.s_addr;
            memcpy(msg.data, buf, r);
            (void)xQueueSend(msg_q, &msg, 0);
        }

        if (bits_q && r >= 5) {
            uint32_t magic;
            memcpy(&magic, buf, sizeof(magic));
            magic = ntohl(magic);

            if (magic == UDP_CMD_MAGIC) {
                uint8_t bits = buf[4] & 0x7F;
                (void)xQueueSend(bits_q, &bits, 0);
            }
        }
    }
}

/* ==================== OTA ====================== */
bool ethernet_ota_init(void)
{
    g_ota_lock = xSemaphoreCreateMutex();
    return g_ota_lock != NULL;
}

static inline void wait_for_eth_ip(void)
{
    ethernet_wait_for_ip();
}

static void send_ack_or_nack(int sock, bool nack, uint32_t image_id, uint32_t seq,
                            const struct sockaddr_in *to, socklen_t tolen)
{
    ota_ack_t a = {0};
    a.h.magic   = OTA_MAGIC;
    a.h.type    = nack ? OTA_MSG_NACK : OTA_MSG_ACK;
    a.h.image_id = image_id;
    a.seq       = seq;

    (void)sendto(sock, &a, sizeof(a), 0, (const struct sockaddr *)to, tolen);
}

static void ota_abort_locked(void)
{
    if (g_ota.active) {
        esp_ota_abort(g_ota.handle);
        g_ota.active = false;
    }
}

static esp_err_t ota_start_locked(const ota_start_t *s)
{
    ota_abort_locked();

    g_ota.image_id      = s->h.image_id;
    g_ota.expected_size = s->image_size;
    g_ota.chunk_size    = s->chunk_size;
    g_ota.written       = 0;
    g_ota.next_seq      = 0;

    g_ota.part = esp_ota_get_next_update_partition(NULL);
    if (!g_ota.part) return ESP_FAIL;

    esp_err_t err = esp_ota_begin(g_ota.part, g_ota.expected_size, &g_ota.handle);
    if (err != ESP_OK) return err;

    g_ota.active = true;

    ESP_LOGI(TAG_OTA_CTRL, "START image_id=%" PRIu32 " size=%" PRIu32 " chunk=%u target=%s",
             g_ota.image_id, g_ota.expected_size, g_ota.chunk_size, g_ota.part->label);

    return ESP_OK;
}

static esp_err_t ota_write_chunk_locked(uint32_t seq, const uint8_t *data, uint16_t len)
{
    if (!g_ota.active) return ESP_ERR_INVALID_STATE;

    if (seq != g_ota.next_seq) return ESP_ERR_INVALID_ARG;

    if (g_ota.expected_size && (g_ota.written + len > g_ota.expected_size)) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = esp_ota_write(g_ota.handle, data, len);
    if (err != ESP_OK) return err;

    g_ota.written += len;
    g_ota.next_seq++;
    return ESP_OK;
}

static esp_err_t ota_finish_locked(void)
{
    if (!g_ota.active) return ESP_ERR_INVALID_STATE;

    if (g_ota.expected_size && g_ota.written != g_ota.expected_size) {
        ESP_LOGW(TAG_OTA_CTRL, "DONE with size mismatch written=%" PRIu32 " expected=%" PRIu32,
                 g_ota.written, g_ota.expected_size);
    }

    esp_err_t err = esp_ota_end(g_ota.handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_OTA_CTRL, "esp_ota_end failed: %s", esp_err_to_name(err));
        ota_abort_locked();
        return err;
    }

    err = esp_ota_set_boot_partition(g_ota.part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_OTA_CTRL, "set_boot_partition failed: %s", esp_err_to_name(err));
        ota_abort_locked();
        return err;
    }

    g_ota.active = false;
    ESP_LOGI(TAG_OTA_CTRL, "OTA success. Rebooting into %s", g_ota.part->label);
    return ESP_OK;
}

void ota_ctrl_task(void *arg)
{
    (void)arg;

    if (!g_ota_lock) {
        ESP_LOGE(TAG_OTA_CTRL, "g_ota_lock is NULL (call ethernet_ota_init)");
        vTaskDelete(NULL);
        return;
    }

    wait_for_eth_ip();

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG_OTA_CTRL, "socket failed errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_port = htons(OTA_CTRL_PORT);
    local.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
        ESP_LOGE(TAG_OTA_CTRL, "bind %d failed errno=%d", OTA_CTRL_PORT, errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG_OTA_CTRL, "Listening on UDP %d (START/DONE/ABORT)", OTA_CTRL_PORT);

    uint8_t rxbuf[256];

    while (1) {
        struct sockaddr_in from = {0};
        socklen_t fromlen = sizeof(from);

        int r = recvfrom(sock, rxbuf, sizeof(rxbuf), 0, (struct sockaddr *)&from, &fromlen);
        if (r < 0) {
            ESP_LOGW(TAG_OTA_CTRL, "recvfrom errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (r < (int)sizeof(ota_hdr_t)) continue;

        const ota_hdr_t *h = (const ota_hdr_t *)rxbuf;
        if (h->magic != OTA_MAGIC) continue;

        if (h->type == OTA_MSG_START) {
            if (r < (int)sizeof(ota_start_t)) {
                ESP_LOGW(TAG_OTA_CTRL, "START too short (%d)", r);
                continue;
            }
            const ota_start_t *s = (const ota_start_t *)rxbuf;

            xSemaphoreTake(g_ota_lock, portMAX_DELAY);
            esp_err_t err = ota_start_locked(s);
            xSemaphoreGive(g_ota_lock);

            ESP_LOGI(TAG_OTA_CTRL, "START from %s:%u -> %s",
                     inet_ntoa(from.sin_addr), ntohs(from.sin_port),
                     esp_err_to_name(err));

        } else if (h->type == OTA_MSG_DONE) {

            xSemaphoreTake(g_ota_lock, portMAX_DELAY);
            esp_err_t err = ota_finish_locked();
            xSemaphoreGive(g_ota_lock);

            ESP_LOGI(TAG_OTA_CTRL, "DONE -> %s", esp_err_to_name(err));

            if (err == ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(50));
                esp_restart();
            }

        } else if (h->type == OTA_MSG_ABORT) {

            xSemaphoreTake(g_ota_lock, portMAX_DELAY);
            ota_abort_locked();
            xSemaphoreGive(g_ota_lock);

            ESP_LOGW(TAG_OTA_CTRL, "ABORT from %s:%u",
                     inet_ntoa(from.sin_addr), ntohs(from.sin_port));
        }
    }
}

void ota_data_task(void *arg)
{
    (void)arg;

    if (!g_ota_lock) {
        ESP_LOGE(TAG_OTA_DATA, "g_ota_lock is NULL (call ethernet_ota_init)");
        vTaskDelete(NULL);
        return;
    }

    wait_for_eth_ip();

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG_OTA_DATA, "socket failed errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_port = htons(OTA_DATA_PORT);
    local.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
        ESP_LOGE(TAG_OTA_DATA, "bind %d failed errno=%d", OTA_DATA_PORT, errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG_OTA_DATA, "Listening on UDP %d (DATA chunks)", OTA_DATA_PORT);

    static uint8_t rxbuf[sizeof(ota_data_t)];

    while (1) {
        struct sockaddr_in from = {0};
        socklen_t fromlen = sizeof(from);

        int r = recvfrom(sock, rxbuf, sizeof(rxbuf), 0, (struct sockaddr *)&from, &fromlen);
        if (r < 0) {
            ESP_LOGW(TAG_OTA_DATA, "recvfrom errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (r < (int)sizeof(ota_hdr_t)) continue;

        ota_hdr_t *h = (ota_hdr_t *)rxbuf;
        if (h->magic != OTA_MAGIC) continue;
        if (h->type != OTA_MSG_DATA) continue;

        const int base = (int)(sizeof(ota_hdr_t) + 4 + 2 + 2);
        if (r < base) continue;

        ota_data_t *d = (ota_data_t *)rxbuf;

        if (d->len > OTA_MAX_PAYLOAD) {
            continue;
        }

        const int need = base + (int)d->len;
        if (r < need) {
            continue;
        }

        bool ack = false, nack = false;
        uint32_t reply_seq = 0;
        uint32_t image_id = d->h.image_id;

        xSemaphoreTake(g_ota_lock, portMAX_DELAY);

        if (!g_ota.active || image_id != g_ota.image_id) {
            if (g_ota.active) {
                nack = true;
                reply_seq = g_ota.next_seq;
            }
        } else {
            esp_err_t err = ota_write_chunk_locked(d->seq, d->data, d->len);
            if (err == ESP_OK) {
                ack = true;
                reply_seq = d->seq;
            } else {
                nack = true;
                reply_seq = g_ota.next_seq;
            }
        }

        xSemaphoreGive(g_ota_lock);

        if (ack) {
            send_ack_or_nack(sock, false, image_id, reply_seq, &from, fromlen);
        } else if (nack) {
            send_ack_or_nack(sock, true, image_id, reply_seq, &from, fromlen);
        }
    }
}
