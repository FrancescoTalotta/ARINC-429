#include "ethernet.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"

#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_eth_netif_glue.h"

#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "arinc.h"

static const char *TAG = "ETH";
static const char *TAG_ARINC_CTRL = "ARINC_CTRL";

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

void udp_scan_status_task(void *arg)
{
    QueueHandle_t q = (QueueHandle_t)arg;
    if (!q) vTaskDelete(NULL);

    ethernet_wait_for_ip();

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) vTaskDelete(NULL);

    struct sockaddr_in pc = {0};
    pc.sin_family = AF_INET;
    pc.sin_port = htons(UDP_SCAN_STATUS_PORT);
    inet_pton(AF_INET, UDP_PC_IP, &pc.sin_addr);

    while (1) {
        arinc_scan_status_t item;
        if (xQueueReceive(q, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        char line[128];
        int len = snprintf(line, sizeof(line),
                           "scan count=%" PRIu32 " label=%03o sdi=%u data=0x%05" PRIX32 " ssm=%u known=%u",
                           item.count, item.label, item.sdi, item.data & 0x7FFFF,
                           item.ssm, item.known ? 1 : 0);
        if (len <= 0) {
            continue;
        }
        if (len >= (int)sizeof(line)) {
            len = sizeof(line) - 1;
        }

        int ret = sendto(sock, line, len, 0, (struct sockaddr *)&pc, sizeof(pc));
        if (ret < 0) {
            ESP_LOGW(TAG_ARINC_CTRL, "scan status sendto failed errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

static bool parse_bool_value(const char *value)
{
    return strcmp(value, "1") == 0 || strcmp(value, "true") == 0 ||
           strcmp(value, "on") == 0 || strcmp(value, "yes") == 0 ||
           strcmp(value, "high") == 0;
}

typedef struct {
    bool clear;
    bool start;
    bool has_all_enabled;
    bool all_enabled;
    bool has_speed;
    bool high_speed;
    bool has_slot;
    uint8_t slot;
    bool has_slot_update;
    arinc_tx_slot_t slot_config;
    bool has_scan_update;
    arinc_scan_config_t scan_config;
} arinc_control_cmd_t;

static bool parse_arinc_control(char *line, arinc_control_cmd_t *cmd, const arinc_tx_slot_t slots[], const arinc_scan_config_t *scan_config)
{
    if (!line || !cmd || !slots || !scan_config) {
        return false;
    }

    *cmd = (arinc_control_cmd_t){0};
    cmd->slot_config = slots[0];
    cmd->scan_config = *scan_config;

    char *saveptr = NULL;
    char *token = strtok_r(line, " \t\r\n,;", &saveptr);
    bool got_any = false;

    while (token) {
        char *eq = strchr(token, '=');
        if (!eq) {
            token = strtok_r(NULL, " \t\r\n,;", &saveptr);
            continue;
        }

        *eq = '\0';
        const char *key = token;
        const char *value = eq + 1;

        if (strcmp(key, "slot") == 0) {
            uint32_t slot = strtoul(value, NULL, 0);
            if (slot >= ARINC_TX_SLOT_COUNT) {
                return false;
            }
            cmd->slot = (uint8_t)slot;
            cmd->has_slot = true;
            cmd->slot_config = slots[cmd->slot];
            got_any = true;
        } else if (strcmp(key, "clear") == 0) {
            cmd->clear = parse_bool_value(value);
            got_any = true;
        } else if (strcmp(key, "start") == 0) {
            cmd->start = parse_bool_value(value);
            got_any = true;
        } else if (strcmp(key, "all_enabled") == 0) {
            cmd->has_all_enabled = true;
            cmd->all_enabled = parse_bool_value(value);
            got_any = true;
        } else if (strcmp(key, "speed") == 0) {
            cmd->has_speed = true;
            cmd->high_speed = strcmp(value, "high") == 0 || strcmp(value, "100k") == 0 || strcmp(value, "1") == 0;
            got_any = true;
        } else if (strcmp(key, "high_speed") == 0) {
            cmd->has_speed = true;
            cmd->high_speed = parse_bool_value(value);
            got_any = true;
        } else if (strcmp(key, "label") == 0) {
            cmd->slot_config.label = (uint8_t)(strtoul(value, NULL, 8) & 0xFF);
            cmd->has_slot_update = true;
            got_any = true;
        } else if (strcmp(key, "sdi") == 0) {
            cmd->slot_config.sdi = (uint8_t)(strtoul(value, NULL, 0) & 0x03);
            cmd->has_slot_update = true;
            got_any = true;
        } else if (strcmp(key, "data") == 0 || strcmp(key, "data19") == 0) {
            cmd->slot_config.data = strtoul(value, NULL, 0) & 0x7FFFF;
            cmd->has_slot_update = true;
            got_any = true;
        } else if (strcmp(key, "ssm") == 0) {
            cmd->slot_config.ssm = (uint8_t)(strtoul(value, NULL, 0) & 0x03);
            cmd->has_slot_update = true;
            got_any = true;
        } else if (strcmp(key, "repeat") == 0 || strcmp(key, "repeat_ms") == 0 || strcmp(key, "period") == 0 || strcmp(key, "period_ms") == 0) {
            uint32_t period_ms = strtoul(value, NULL, 0);
            if (period_ms > 5000) period_ms = 5000;
            cmd->slot_config.period_ms = (uint16_t)period_ms;
            cmd->has_slot_update = true;
            got_any = true;
        } else if (strcmp(key, "offset") == 0 || strcmp(key, "offset_ms") == 0) {
            uint32_t offset_ms = strtoul(value, NULL, 0);
            if (offset_ms > 5000) offset_ms = 5000;
            cmd->slot_config.offset_ms = (uint16_t)offset_ms;
            cmd->has_slot_update = true;
            got_any = true;
        } else if (strcmp(key, "enabled") == 0 || strcmp(key, "enable") == 0) {
            cmd->slot_config.enabled = parse_bool_value(value);
            cmd->has_slot_update = true;
            got_any = true;
        } else if (strcmp(key, "once") == 0 || strcmp(key, "send_once") == 0) {
            cmd->slot_config.send_once = parse_bool_value(value);
            cmd->has_slot_update = true;
            got_any = true;
        } else if (strcmp(key, "scan_enabled") == 0 || strcmp(key, "scan_enable") == 0) {
            cmd->scan_config.enabled = parse_bool_value(value);
            cmd->has_scan_update = true;
            got_any = true;
        } else if (strcmp(key, "scan_start") == 0 || strcmp(key, "scan_start_label") == 0) {
            cmd->scan_config.start_label = (uint8_t)(strtoul(value, NULL, 8) & 0xFF);
            cmd->has_scan_update = true;
            got_any = true;
        } else if (strcmp(key, "scan_end") == 0 || strcmp(key, "scan_end_label") == 0) {
            cmd->scan_config.end_label = (uint8_t)(strtoul(value, NULL, 8) & 0xFF);
            cmd->has_scan_update = true;
            got_any = true;
        } else if (strcmp(key, "scan_sdi") == 0) {
            cmd->scan_config.sdi = (uint8_t)(strtoul(value, NULL, 0) & 0x03);
            cmd->has_scan_update = true;
            got_any = true;
        } else if (strcmp(key, "scan_period") == 0 || strcmp(key, "scan_period_ms") == 0) {
            uint32_t period_ms = strtoul(value, NULL, 0);
            if (period_ms > 5000) period_ms = 5000;
            cmd->scan_config.period_ms = (uint16_t)period_ms;
            cmd->has_scan_update = true;
            got_any = true;
        } else if (strcmp(key, "scan_dwell") == 0 || strcmp(key, "scan_dwell_ms") == 0) {
            uint32_t dwell_ms = strtoul(value, NULL, 0);
            if (dwell_ms > 60000) dwell_ms = 60000;
            cmd->scan_config.dwell_ms = (uint16_t)dwell_ms;
            cmd->has_scan_update = true;
            got_any = true;
        }

        token = strtok_r(NULL, " \t\r\n,;", &saveptr);
    }

    return got_any;
}

static void send_arinc_control_reply(int sock, const struct sockaddr_in *to, socklen_t tolen,
                                     bool ok, const char *detail)
{
    char reply[128];
    int len = snprintf(reply, sizeof(reply), "%s %s", ok ? "OK" : "ERR", detail ? detail : "");
    if (len <= 0) {
        return;
    }
    if (len >= (int)sizeof(reply)) {
        len = sizeof(reply) - 1;
    }

    int ret = sendto(sock, reply, len, 0, (const struct sockaddr *)to, tolen);
    if (ret < 0) {
        ESP_LOGW(TAG_ARINC_CTRL, "reply sendto failed errno=%d", errno);
    }
}

void udp_arinc_control_task(void *arg)
{
    (void)arg;

    ethernet_wait_for_ip();

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG_ARINC_CTRL, "socket failed errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_port = htons(UDP_ARINC_CTRL_PORT);
    local.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
        ESP_LOGE(TAG_ARINC_CTRL, "bind %d failed errno=%d", UDP_ARINC_CTRL_PORT, errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG_ARINC_CTRL, "Listening on UDP %d for ARINC TX commands", UDP_ARINC_CTRL_PORT);

    arinc_tx_slot_t slots[ARINC_TX_SLOT_COUNT] = {0};
    for (uint8_t i = 0; i < ARINC_TX_SLOT_COUNT; i++) {
        slots[i].period_ms = 50;
    }
    slots[0] = (arinc_tx_slot_t){
        .label = 0320,
        .sdi = 0,
        .ssm = 3,
        .data = 90,
        .period_ms = 50,
        .offset_ms = 0,
        .enabled = false,
        .send_once = false,
    };
    arinc_scan_config_t scan_config = {
        .start_label = 0000,
        .end_label = 0377,
        .sdi = 0,
        .period_ms = 50,
        .dwell_ms = 500,
        .enabled = false,
    };

    char buf[256];

    while (1) {
        struct sockaddr_in from = {0};
        socklen_t fromlen = sizeof(from);
        int r = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from, &fromlen);
        if (r < 0) {
            ESP_LOGW(TAG_ARINC_CTRL, "recvfrom failed errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        buf[r] = '\0';
        char original[sizeof(buf)];
        snprintf(original, sizeof(original), "%s", buf);

        arinc_control_cmd_t cmd;
        if (!parse_arinc_control(buf, &cmd, slots, &scan_config)) {
            ESP_LOGW(TAG_ARINC_CTRL, "Ignored invalid command");
            send_arinc_control_reply(sock, &from, fromlen, false, "invalid command");
            continue;
        }

        if (cmd.clear) {
            for (uint8_t i = 0; i < ARINC_TX_SLOT_COUNT; i++) {
                slots[i] = (arinc_tx_slot_t){ .period_ms = 50 };
            }
            arinc_clear_tx_slots();
            ESP_LOGI(TAG_ARINC_CTRL, "Cleared all ARINC TX slots");
        }
        if (cmd.has_speed) {
            arinc_set_high_speed(cmd.high_speed);
            ESP_LOGI(TAG_ARINC_CTRL, "Global ARINC speed=%s", cmd.high_speed ? "high" : "low");
        }
        if (cmd.has_all_enabled) {
            for (uint8_t i = 0; i < ARINC_TX_SLOT_COUNT; i++) {
                slots[i].enabled = cmd.all_enabled;
            }
            arinc_set_all_tx_slots_enabled(cmd.all_enabled);
            ESP_LOGI(TAG_ARINC_CTRL, "All ARINC TX slots enabled=%u", cmd.all_enabled);
        }
        if (cmd.start) {
            arinc_start_tx_scheduler();
            ESP_LOGI(TAG_ARINC_CTRL, "ARINC TX scheduler started/aligned");
        }
        if (cmd.has_slot_update) {
            uint8_t slot = cmd.has_slot ? cmd.slot : 0;
            slots[slot] = cmd.slot_config;
            arinc_update_tx_slot(slot, &slots[slot]);
            ESP_LOGI(TAG_ARINC_CTRL, "slot=%u label=%03o sdi=%u data=0x%05" PRIX32 " ssm=%u period=%u offset=%u enabled=%u once=%u",
                     slot, slots[slot].label, slots[slot].sdi, slots[slot].data,
                     slots[slot].ssm, slots[slot].period_ms, slots[slot].offset_ms,
                     slots[slot].enabled, slots[slot].send_once);
        }
        if (cmd.has_scan_update) {
            scan_config = cmd.scan_config;
            arinc_update_scan_config(&scan_config);
            ESP_LOGI(TAG_ARINC_CTRL, "scan enabled=%u start=%03o end=%03o sdi=%u period=%u dwell=%u",
                     scan_config.enabled, scan_config.start_label, scan_config.end_label,
                     scan_config.sdi, scan_config.period_ms, scan_config.dwell_ms);
        }

        send_arinc_control_reply(sock, &from, fromlen, true, original);
    }
}
