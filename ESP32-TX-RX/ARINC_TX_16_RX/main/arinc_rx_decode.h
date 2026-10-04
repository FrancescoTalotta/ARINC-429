#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t label;
    uint8_t sdi;
    uint8_t ssm;
    uint32_t data19;
    bool parity_ok;
} arinc_rx_decoded_t;

/* ACP capture shifts the first sampled bit into raw[31]. Match ARINC_TX_16's
 * encoder, NOT a host-order ARINC bit-number diagram: label=raw[31:24],
 * SDI=raw[23:22], SSM=raw[2:1], parity bit=raw[0]. Do not reverse label,
 * SDI or SSM. Reverse ONLY raw[21:3] to recover the TX API's data19 value.
 * parity_ok means odd parity over all 32 bits, not the parity bit's value.
 * The caller retains raw unchanged. This helper is portable, task-side C. */
static inline arinc_rx_decoded_t arinc_rx_decode(uint32_t raw)
{
    arinc_rx_decoded_t decoded = {
        .label = (uint8_t)(raw >> 24),
        .sdi = (uint8_t)((raw >> 22) & 3U),
        .ssm = (uint8_t)((raw >> 1) & 3U),
    };
    uint32_t captured_data = (raw >> 3) & 0x7FFFFU;
    for (unsigned i = 0; i < 19; ++i) {
        decoded.data19 = (decoded.data19 << 1) | (captured_data & 1U);
        captured_data >>= 1;
    }
    uint32_t parity = raw;
    parity ^= parity >> 16;
    parity ^= parity >> 8;
    parity ^= parity >> 4;
    parity ^= parity >> 2;
    parity ^= parity >> 1;
    decoded.parity_ok = (parity & 1U) != 0;
    return decoded;
}
