#pragma once

/* ---------- UDP test config ---------- */
#define UDP_PC_IP          "192.168.1.13"  // set to PC IP
#define UDP_SCAN_STATUS_PORT 5003
#define UDP_ARINC_CTRL_PORT 5002

void ethernet_w5500_init(void);
void ethernet_wait_for_ip(void);

void udp_scan_status_task(void *arg);
void udp_arinc_control_task(void *arg);
