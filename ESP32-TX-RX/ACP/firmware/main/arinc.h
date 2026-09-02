#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* ------------------ ARINC GPIO/RMT ------------------ */
#define SCLK_GPIO       13
#define MOSI_GPIO       14
#define RMT_RES_HZ      1000000    // 1 tick = 1 us
#define HALF_PERIOD_US  40

/* --- Pins for capture (receiver) --- */
#define PIN_SCLK_RX       11      // external clock IN
#define PIN_DATA_RX       12      // external data IN

/* Idle gap to mark end-of-frame and resync (us) */
#define RESYNC_GAP_US     4000

#define WORD_BITS         32

typedef struct {
    uint8_t  label;
    uint8_t  sdi;
    uint8_t  ssm;
    uint32_t data;
    uint32_t word;
} arinc_msg_t;

bool arinc_queues_init(void);

QueueHandle_t arinc_get_filtered_queue(void);
QueueHandle_t arinc_get_filtered_queue_udp(void);

void arinc_tx_hw_init(void);
void arinc_rx_hw_init(void);

void arinc_tx_task(void *arg);      // arg: QueueHandle_t for UDP bits (may be NULL)
void arinc_filter_task(void *arg);
void arinc_sclk_isr(void *arg);
