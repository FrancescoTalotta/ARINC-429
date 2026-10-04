#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "arinc.h"
#include "arinc_rx.h"
#include "ethernet.h"

static const char *TAG = "ARINC_ETH";

/* ======================= APP ======================= */
void app_main(void)
{
    ESP_LOGI(TAG, "ARINC 429 TX + passive RX over Ethernet UDP control");

    if (!arinc_queues_init()) {
        ESP_LOGE(TAG, "Failed to create ARINC queues");
        return;
    }

    /* Own the IRAM GPIO service before Ethernet installs its handler. RX starts
     * and drains before DHCP; there is no RX-to-TX path. */
    ESP_ERROR_CHECK(arinc_rx_init());

    ESP_LOGI(TAG, "Starting W5500 Ethernet (DHCP) + UDP (ESP-IDF v5.5)");
    ethernet_w5500_init();

    ESP_LOGI(TAG, "Waiting for DHCP (IP_EVENT_ETH_GOT_IP)...");
    ethernet_wait_for_ip();

    arinc_tx_hw_init();

    ESP_LOGI(TAG, "Got IP. Starting tasks: ARINC UDP control TX.");

    xTaskCreate(udp_scan_status_task, "scan_status", 4096, (void *)arinc_get_scan_status_queue(), 4, NULL);
    xTaskCreate(udp_arinc_control_task, "arinc_ctrl", 4096, NULL, 4, NULL);
    xTaskCreate(arinc_control_tx_task, "arinc_ctrl_tx", 4096, NULL, 4, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
