#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Call once, before Ethernet, to own an IRAM GPIO interrupt service. Starts
 * capture and a draining task immediately, even without a network/IP address.
 * Default RX speed is high (100 kbit/s); default subscription is disabled. */
esp_err_t arinc_rx_init(void);

/* Speed changes discard the partial frame and wait for a new idle gap.
 * Completed queued words and lifetime counters are retained. */
void arinc_rx_set_high_speed(bool high_speed);

/* IPv4 address in network byte order, port in host byte order. Port zero
 * disables telemetry, not capture. A new subscription resets the per-label/SDI
 * change filter so the first following word for every key is reported. Single
 * subscriber, retained until changed or reboot. A previously snapshotted batch
 * may still go to the old target. */
void arinc_rx_subscribe(uint32_t ipv4, uint16_t port);
