#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* ------------------ ARINC GPIO/RMT ------------------ */
#define SCLK_GPIO       13
#define MOSI_GPIO       14
#define RMT_RES_HZ      1000000    // 1 tick = 1 us
#define ARINC_HIGH_SPEED_HALF_PERIOD_US  5
#define ARINC_LOW_SPEED_HALF_PERIOD_US   40
#define ARINC_TX_SLOT_COUNT              16

typedef struct {
    uint8_t  label;
    uint8_t  sdi;
    uint8_t  ssm;
    uint32_t data;
    uint16_t period_ms;
    uint16_t offset_ms;
    bool     enabled;
    bool     send_once;
} arinc_tx_slot_t;

typedef struct {
    uint8_t  start_label;
    uint8_t  end_label;
    uint8_t  sdi;
    uint16_t period_ms;
    uint16_t dwell_ms;
    bool     enabled;
} arinc_scan_config_t;

typedef struct {
    uint8_t  label;
    uint8_t  sdi;
    uint8_t  ssm;
    uint32_t data;
    bool     known;
    uint32_t count;
} arinc_scan_status_t;

bool arinc_queues_init(void);

QueueHandle_t arinc_get_scan_status_queue(void);

void arinc_tx_hw_init(void);
void arinc_send_data(uint8_t label, uint8_t sdi, uint32_t data19, uint8_t ssm);
void arinc_set_high_speed(bool high_speed);
bool arinc_update_tx_slot(uint8_t slot, const arinc_tx_slot_t *config);
void arinc_clear_tx_slots(void);
void arinc_set_all_tx_slots_enabled(bool enabled);
void arinc_update_scan_config(const arinc_scan_config_t *config);
void arinc_start_tx_scheduler(void);

void arinc_control_tx_task(void *arg);
