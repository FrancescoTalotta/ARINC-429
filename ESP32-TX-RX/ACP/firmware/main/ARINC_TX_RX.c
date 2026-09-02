#include <inttypes.h>
#include <string.h>
#include <arpa/inet.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_ota_ops.h"

#include "arinc.h"
#include "ethernet.h"

static const char *TAG = "APP";

static udp_rx_task_cfg_t s_udp_rx_cfg;

/* ======================= APP ======================= */
void app_main(void)
{
    ESP_LOGI(TAG, "ARINC 429 32-bit TX-RX");

    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI("BOOT", "Running partition: %s", running->label);

    if (!arinc_queues_init()) {
        ESP_LOGE(TAG, "Failed to create ARINC queues");
        return;
    }

    QueueHandle_t udp_rx_queue = ethernet_udp_rx_queue_create();
    QueueHandle_t udp_bits_queue = ethernet_udp_bits_queue_create();
    if (!udp_rx_queue || !udp_bits_queue) {
        ESP_LOGE(TAG, "Failed to create UDP queues");
        return;
    }

    ESP_LOGI(TAG, "Starting W5500 Ethernet (DHCP) + UDP (ESP-IDF v6.0)");
    ethernet_w5500_init();

    ESP_LOGI(TAG, "Waiting for DHCP (IP_EVENT_ETH_GOT_IP)...");
    ethernet_wait_for_ip();

    arinc_tx_hw_init();
    arinc_rx_hw_init();

    ESP_LOGI(TAG, "Got IP. Starting tasks: GPIO ISR, ARINC FILTER, UDP.");

    xTaskCreate(arinc_tx_task, "arinc_tx_task", 4096, (void *)udp_bits_queue, 5, NULL);
    xTaskCreate(arinc_filter_task, "arinc_print", 4096, NULL, 5, NULL);
    xTaskCreate(udp_tx_task, "udp_task", 4096, (void *)arinc_get_filtered_queue_udp(), 4, NULL);

    s_udp_rx_cfg.msg_queue = udp_rx_queue;
    s_udp_rx_cfg.bits_queue = udp_bits_queue;
    xTaskCreate(udp_rx_task, "udp_rx_task", 4096, (void *)&s_udp_rx_cfg, 4, NULL);

    if (!ethernet_ota_init()) {
        ESP_LOGE(TAG, "Failed to create OTA mutex");
        return;
    }
    xTaskCreate(ota_ctrl_task, "ota_ctrl", 4096, NULL, 3, NULL);
    xTaskCreate(ota_data_task, "ota_data", 6144, NULL, 3, NULL);

    ESP_LOGI(TAG, "External mode: connect your front-end SCLK->GPIO%d, DATA->GPIO%d",
             PIN_SCLK_RX, PIN_DATA_RX);

    while (1) {
        udp_rx_msg_t rx;

        if (xQueueReceive(udp_rx_queue, &rx, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (rx.len >= 4) {
                uint32_t v;
                memcpy(&v, rx.data, 4);
                v = ntohl(v);

                ESP_LOGI("CMD", "Got 0x%08" PRIX32 " from %u.%u.%u.%u:%u (len=%u)",
                         v,
                         (rx.from_ip      ) & 0xFF,
                         (rx.from_ip >>  8) & 0xFF,
                         (rx.from_ip >> 16) & 0xFF,
                         (rx.from_ip >> 24) & 0xFF,
                         rx.from_port,
                         rx.len);
            }
        }
    }
}
