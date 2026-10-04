#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "arinc_rx_decode.h"

#define ARINC_RX_FILTER_KEY_COUNT (256U * 4U)

typedef struct {
    uint32_t data19[ARINC_RX_FILTER_KEY_COUNT];
    uint8_t state[ARINC_RX_FILTER_KEY_COUNT];
} arinc_rx_filter_t;

static inline void arinc_rx_filter_reset(arinc_rx_filter_t *filter)
{
    memset(filter, 0, sizeof(*filter));
}

/* Match ACP behavior: label and SDI identify a stream. Report its first word,
 * then only Data19 or SSM changes. Raw/parity-only changes are suppressed. */
static inline bool arinc_rx_filter_changed(arinc_rx_filter_t *filter,
                                           const arinc_rx_decoded_t *word)
{
    unsigned key = (unsigned)word->label * 4U + word->sdi;
    uint8_t state = (uint8_t)(0x80U | word->ssm);
    if (filter->state[key] != 0U && filter->data19[key] == word->data19 &&
        (filter->state[key] & 3U) == word->ssm) {
        return false;
    }
    filter->data19[key] = word->data19;
    filter->state[key] = state;
    return true;
}
