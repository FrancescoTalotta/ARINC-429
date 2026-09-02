#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* ---------- UDP test config ---------- */
#define UDP_RX_PORT        5000
#define UDP_PC_IP          "192.168.1.13" // set to PC IP
#define UDP_PC_PORT        5001           // set to PC listening port
#define UDP_RX_MAX_PAYLOAD 512
#define UDP_RX_QUEUE_LEN   16
#define UDP_BITS_QUEUE_LEN 32

typedef struct {
    uint16_t len;
    uint16_t from_port;     // host order
    uint32_t from_ip;       // network order (sin_addr.s_addr)
    uint8_t  data[UDP_RX_MAX_PAYLOAD];
} udp_rx_msg_t;

typedef struct {
    QueueHandle_t msg_queue;
    QueueHandle_t bits_queue;
} udp_rx_task_cfg_t;

void ethernet_w5500_init(void);
void ethernet_wait_for_ip(void);

QueueHandle_t ethernet_udp_rx_queue_create(void);
QueueHandle_t ethernet_udp_bits_queue_create(void);

void udp_tx_task(void *arg);
void udp_rx_task(void *arg);

bool ethernet_ota_init(void);
void ota_ctrl_task(void *arg);
void ota_data_task(void *arg);
