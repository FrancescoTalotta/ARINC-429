#include "arinc.h"

#include <stddef.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static const char *TAG = "ARINC_TX";

/* ======================= STATE ======================= */
static rmt_channel_handle_t clk_chan = NULL;
static rmt_channel_handle_t mosi_chan = NULL;
static rmt_encoder_handle_t clk_enc  = NULL;
static rmt_encoder_handle_t mosi_enc = NULL;

static rmt_symbol_word_t clk_items[32];
static rmt_symbol_word_t mosi_items[32];
static rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
static rmt_sync_manager_handle_t sync_mgr = NULL;
static SemaphoreHandle_t arinc_tx_lock;
static uint32_t s_half_period_us = ARINC_HIGH_SPEED_HALF_PERIOD_US;
static arinc_tx_slot_t s_tx_slots[ARINC_TX_SLOT_COUNT];
static uint32_t s_tx_slot_due_ms[ARINC_TX_SLOT_COUNT];
static uint32_t s_scheduler_start_ms;
static arinc_scan_config_t s_scan_config;
static uint8_t s_scan_label;
static uint8_t s_scan_unknown_ssm;
static uint32_t s_scan_due_ms;
static uint32_t s_scan_dwell_due_ms;
static uint32_t s_scan_sent_count;
static uint8_t s_tx_rr_next;

static QueueHandle_t scan_status_queue;

typedef struct {
    uint8_t label;
    uint32_t data19;
    uint8_t ssm;
} arinc_label_default_t;

static const arinc_label_default_t s_label_defaults[] = {
    { 0001, 0, 0 }, { 0002, 0, 0 }, { 0010, 0, 0 }, { 0011, 0, 0 },
    { 0012, 0, 0 }, { 0013, 0, 0 }, { 0014, 0, 0 }, { 0015, 0, 0 },
    { 0016, 0, 0 }, { 0017, 0, 0 }, { 0024, 0, 0 }, { 0027, 0, 0 },
    { 0030, 0, 0 }, { 0031, 0, 0 }, { 0032, 0, 0 }, { 0033, 0, 0 },
    { 0034, 0, 0 }, { 0035, 0, 0 }, { 0040, 0, 0 }, { 0041, 0, 0 },
    { 0042, 0, 0 }, { 0043, 0, 0 }, { 0044, 0, 0 }, { 0060, 0, 3 },
    { 0061, 0, 3 }, { 0074, 0, 0 }, { 0075, 0, 0 }, { 0100, 0, 3 },
    { 0101, 0, 3 }, { 0102, 0, 3 }, { 0103, 0, 3 }, { 0104, 0, 3 },
    { 0105, 0, 3 }, { 0106, 0, 3 }, { 0110, 0, 3 }, { 0111, 0, 3 },
    { 0113, 0, 3 }, { 0114, 0, 3 }, { 0115, 0, 3 }, { 0116, 0, 3 },
    { 0117, 0, 3 }, { 0121, 0, 3 }, { 0122, 0, 3 }, { 0123, 0, 3 },
    { 0125, 0, 0 }, { 0147, 0, 3 }, { 0150, 0, 3 }, { 0157, 0, 3 },
    { 0162, 0, 3 }, { 0163, 0, 3 }, { 0164, 0, 3 }, { 0165, 0, 0 },
    { 0173, 0, 3 }, { 0174, 0, 3 }, { 0201, 0, 0 }, { 0202, 0, 3 },
    { 0203, 0, 3 }, { 0204, 0, 3 }, { 0205, 0, 3 }, { 0206, 0, 3 },
    { 0207, 0, 3 }, { 0210, 0, 3 }, { 0211, 0, 3 }, { 0212, 0, 3 },
    { 0213, 0, 3 }, { 0215, 0, 3 }, { 0220, 0, 3 }, { 0221, 0, 3 },
    { 0222, 0, 3 }, { 0223, 0, 3 }, { 0230, 0, 0 }, { 0231, 0, 0 },
    { 0233, 0, 0 }, { 0234, 0, 0 }, { 0236, 0, 0 }, { 0241, 0, 3 },
    { 0242, 0, 3 }, { 0246, 0, 3 }, { 0251, 0, 3 }, { 0252, 0, 3 },
    { 0260, 0, 0 }, { 0261, 0, 0 }, { 0270, 0, 0 }, { 0271, 0, 0 },
    { 0272, 0, 0 }, { 0273, 0, 0 }, { 0274, 0, 0 }, { 0275, 0, 0 },
    { 0276, 0, 0 }, { 0277, 0, 0 }, { 0300, 0, 3 }, { 0301, 0, 3 },
    { 0302, 0, 3 }, { 0303, 0, 3 }, { 0304, 0, 3 }, { 0305, 0, 3 },
    { 0306, 0, 3 }, { 0307, 0, 3 }, { 0310, 0, 3 }, { 0311, 0, 3 },
    { 0312, 0, 3 }, { 0313, 0, 3 }, { 0314, 0, 3 }, { 0315, 0, 3 },
    { 0316, 0, 3 }, { 0320, 0, 3 }, { 0321, 0, 3 }, { 0324, 0, 3 },
    { 0325, 0, 3 }, { 0326, 0, 3 }, { 0327, 0, 3 }, { 0330, 0, 3 },
    { 0331, 0, 3 }, { 0332, 0, 3 }, { 0333, 0, 3 }, { 0334, 0, 3 },
    { 0335, 0, 3 }, { 0340, 0, 3 }, { 0351, 0, 3 }, { 0352, 0, 3 },
    { 0353, 0, 0 }, { 0360, 0, 3 }, { 0361, 0, 3 }, { 0362, 0, 3 },
    { 0363, 0, 3 }, { 0364, 0, 3 }, { 0365, 0, 3 }, { 0366, 0, 3 },
    { 0367, 0, 3 }, { 0371, 0, 0 }, { 0377, 0, 0 },
};

/* ===================== ARINC TX ==================== */
static inline uint32_t reverse_bits(uint32_t x, uint8_t n, uint32_t pad_right, uint32_t pad_left)
{
    uint32_t r = 0;
    for (uint8_t i = 0; i < n; i++) {
       r <<= 1;
       r |= (x >> i) & 1U;
    }
    r = r >> pad_right;
    r = r << pad_left;
    return r;
}

static unsigned int ARINC429_ComputeMSBParity(unsigned long X)
{
    unsigned int parity = 0;
    unsigned int i;
    unsigned long mask;

    for (i = 0, mask = 1; i < 32; i++, mask <<= 1) {
        if (X & mask) {
            parity++;
        }
    }
    return ((parity + 1) & 1);
}

static void update_clk_items(uint32_t half_period_us)
{
    for (int i = 0; i < 32; i++) {
        clk_items[i].level0 = 0;
        clk_items[i].duration0 = half_period_us;
        clk_items[i].level1 = 1;
        clk_items[i].duration1 = half_period_us;
    }
}

static TickType_t ms_to_ticks_min_1(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);
    return ticks ? ticks : 1;
}

static inline void SEND_ARINC429_DATA(uint8_t label, uint8_t sdi, uint32_t data19_local, uint8_t ssm)
{
    data19_local = reverse_bits(data19_local, 19, 0, 0);
    uint32_t half_period_us = s_half_period_us;

    uint32_t w31 = ((uint32_t)label  << 24)
                 | ((uint32_t)sdi    << 22)
                 | ((uint32_t)data19_local << 3)
                 | ((uint32_t)ssm    << 1);

    uint32_t w32 = (w31 & 0xFFFFFFFEUL) | ((uint32_t)ARINC429_ComputeMSBParity(w31));

    for (int i = 0; i < 32; i++) {
        int bit = (w32 >> (31 - i)) & 0x1;
        mosi_items[i].level0 = bit;
        mosi_items[i].duration0 = half_period_us;
        mosi_items[i].level1 = bit;
        mosi_items[i].duration1 = half_period_us;
    }

    rmt_sync_reset(sync_mgr);
    esp_err_t err = rmt_transmit(clk_chan, clk_enc, clk_items, sizeof(clk_items), &tx_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RMT clock transmit failed: %s", esp_err_to_name(err));
        return;
    }

    err = rmt_transmit(mosi_chan, mosi_enc, mosi_items, sizeof(mosi_items), &tx_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RMT data transmit failed: %s", esp_err_to_name(err));
        return;
    }

    err = rmt_tx_wait_all_done(mosi_chan, ms_to_ticks_min_1(100));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RMT TX wait failed: %s", esp_err_to_name(err));
    }
}

void arinc_send_data(uint8_t label, uint8_t sdi, uint32_t data19_local, uint8_t ssm)
{
    if (arinc_tx_lock) {
        xSemaphoreTake(arinc_tx_lock, portMAX_DELAY);
    }
    SEND_ARINC429_DATA(label, sdi, data19_local, ssm);
    if (arinc_tx_lock) {
        xSemaphoreGive(arinc_tx_lock);
    }
}

void arinc_set_high_speed(bool high_speed)
{
    uint32_t half_period_us = high_speed ? ARINC_HIGH_SPEED_HALF_PERIOD_US : ARINC_LOW_SPEED_HALF_PERIOD_US;

    if (arinc_tx_lock) {
        xSemaphoreTake(arinc_tx_lock, portMAX_DELAY);
    }
    s_half_period_us = half_period_us;
    update_clk_items(half_period_us);
    if (arinc_tx_lock) {
        xSemaphoreGive(arinc_tx_lock);
    }
}

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

bool arinc_update_tx_slot(uint8_t slot, const arinc_tx_slot_t *config)
{
    if (slot >= ARINC_TX_SLOT_COUNT || !config) {
        return false;
    }

    if (arinc_tx_lock) {
        xSemaphoreTake(arinc_tx_lock, portMAX_DELAY);
    }
    s_tx_slots[slot] = *config;
    s_tx_slots[slot].sdi &= 0x03;
    s_tx_slots[slot].ssm &= 0x03;
    s_tx_slots[slot].data &= 0x7FFFF;
    if (s_tx_slots[slot].period_ms == 0) {
        s_tx_slots[slot].period_ms = 50;
    }
    s_tx_slot_due_ms[slot] = s_scheduler_start_ms + s_tx_slots[slot].offset_ms;
    if (arinc_tx_lock) {
        xSemaphoreGive(arinc_tx_lock);
    }

    return true;
}

void arinc_clear_tx_slots(void)
{
    if (arinc_tx_lock) {
        xSemaphoreTake(arinc_tx_lock, portMAX_DELAY);
    }
    for (uint8_t i = 0; i < ARINC_TX_SLOT_COUNT; i++) {
        s_tx_slots[i] = (arinc_tx_slot_t){0};
        s_tx_slots[i].period_ms = 50;
        s_tx_slot_due_ms[i] = s_scheduler_start_ms;
    }
    if (arinc_tx_lock) {
        xSemaphoreGive(arinc_tx_lock);
    }
}

void arinc_set_all_tx_slots_enabled(bool enabled)
{
    if (arinc_tx_lock) {
        xSemaphoreTake(arinc_tx_lock, portMAX_DELAY);
    }
    for (uint8_t i = 0; i < ARINC_TX_SLOT_COUNT; i++) {
        s_tx_slots[i].enabled = enabled;
        s_tx_slot_due_ms[i] = s_scheduler_start_ms + s_tx_slots[i].offset_ms;
    }
    if (arinc_tx_lock) {
        xSemaphoreGive(arinc_tx_lock);
    }
}

static bool label_in_scan_range(uint8_t label, uint8_t start_label, uint8_t end_label)
{
    if (start_label <= end_label) {
        return label >= start_label && label <= end_label;
    }
    return label >= start_label || label <= end_label;
}

static uint8_t next_scan_label(uint8_t label, uint8_t start_label, uint8_t end_label)
{
    uint8_t next = label + 1;
    if (!label_in_scan_range(next, start_label, end_label)) {
        next = start_label;
    }
    return next;
}

static bool find_label_default(uint8_t label, uint32_t *data19_out, uint8_t *ssm_out)
{
    for (size_t i = 0; i < sizeof(s_label_defaults) / sizeof(s_label_defaults[0]); i++) {
        if (s_label_defaults[i].label == label) {
            if (data19_out) *data19_out = s_label_defaults[i].data19;
            if (ssm_out) *ssm_out = s_label_defaults[i].ssm;
            return true;
        }
    }
    return false;
}

void arinc_update_scan_config(const arinc_scan_config_t *config)
{
    if (!config) {
        return;
    }

    if (arinc_tx_lock) {
        xSemaphoreTake(arinc_tx_lock, portMAX_DELAY);
    }
    s_scan_config = *config;
    s_scan_config.sdi &= 0x03;
    if (s_scan_config.period_ms == 0) {
        s_scan_config.period_ms = 50;
    }
    if (s_scan_config.period_ms > 5000) {
        s_scan_config.period_ms = 5000;
    }
    if (s_scan_config.dwell_ms < s_scan_config.period_ms) {
        s_scan_config.dwell_ms = s_scan_config.period_ms;
    }
    if (s_scan_config.dwell_ms > 60000) {
        s_scan_config.dwell_ms = 60000;
    }
    if (!label_in_scan_range(s_scan_label, s_scan_config.start_label, s_scan_config.end_label)) {
        s_scan_label = s_scan_config.start_label;
    }
    s_scan_due_ms = now_ms();
    s_scan_dwell_due_ms = s_scan_due_ms + s_scan_config.dwell_ms;
    if (arinc_tx_lock) {
        xSemaphoreGive(arinc_tx_lock);
    }
}

void arinc_start_tx_scheduler(void)
{
    uint32_t start_ms = now_ms();

    if (arinc_tx_lock) {
        xSemaphoreTake(arinc_tx_lock, portMAX_DELAY);
    }
    s_scheduler_start_ms = start_ms;
    for (uint8_t i = 0; i < ARINC_TX_SLOT_COUNT; i++) {
        s_tx_slot_due_ms[i] = start_ms + s_tx_slots[i].offset_ms;
    }
    s_scan_due_ms = start_ms;
    s_scan_dwell_due_ms = start_ms + s_scan_config.dwell_ms;
    if (arinc_tx_lock) {
        xSemaphoreGive(arinc_tx_lock);
    }
}

/* =================== ARINC QUEUES ======================= */
bool arinc_queues_init(void)
{
    arinc_tx_lock = xSemaphoreCreateMutex();
    s_scheduler_start_ms = now_ms();
    s_scan_config = (arinc_scan_config_t){
        .start_label = 0000,
        .end_label = 0377,
        .sdi = 0,
        .period_ms = 50,
        .dwell_ms = 500,
        .enabled = false,
    };
    s_scan_label = s_scan_config.start_label;
    s_scan_unknown_ssm = 0;
    s_scan_sent_count = 0;
    s_scan_due_ms = s_scheduler_start_ms;
    s_scan_dwell_due_ms = s_scheduler_start_ms + s_scan_config.dwell_ms;
    s_tx_rr_next = 0;
    arinc_clear_tx_slots();
    scan_status_queue   = xQueueCreate(1, sizeof(arinc_scan_status_t));

    return arinc_tx_lock && scan_status_queue;
}

QueueHandle_t arinc_get_scan_status_queue(void)
{
    return scan_status_queue;
}

/* ======================= ARINC TX INIT ======================= */
void arinc_tx_hw_init(void)
{
    rmt_tx_channel_config_t clk_cfg = {
        .gpio_num = SCLK_GPIO,
        .clk_src = RMT_CLK_SRC_APB,
        .resolution_hz = RMT_RES_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 5,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&clk_cfg, &clk_chan));

    rmt_tx_channel_config_t mosi_cfg = {
        .gpio_num = MOSI_GPIO,
        .clk_src = RMT_CLK_SRC_APB,
        .resolution_hz = RMT_RES_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 5,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&mosi_cfg, &mosi_chan));

    rmt_copy_encoder_config_t enc_cfg = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&enc_cfg, &clk_enc));
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&enc_cfg, &mosi_enc));

    ESP_ERROR_CHECK(rmt_enable(clk_chan));
    ESP_ERROR_CHECK(rmt_enable(mosi_chan));

    rmt_channel_handle_t chans[2] = { clk_chan, mosi_chan };
    rmt_sync_manager_config_t sync_cfg = {
        .tx_channel_array = chans,
        .array_size = 2,
    };
    ESP_ERROR_CHECK(rmt_new_sync_manager(&sync_cfg, &sync_mgr));

    update_clk_items(s_half_period_us);
}

void arinc_control_tx_task(void *arg)
{
    (void)arg;

    while (1) {
        arinc_tx_slot_t tx_slot = {0};
        arinc_scan_status_t scan_status = {0};
        bool have_tx = false;
        bool have_scan_status = false;
        uint32_t current_ms = now_ms();

        xSemaphoreTake(arinc_tx_lock, portMAX_DELAY);
        for (uint8_t check = 0; check < ARINC_TX_SLOT_COUNT + 1; check++) {
            uint8_t channel = (s_tx_rr_next + check) % (ARINC_TX_SLOT_COUNT + 1);
            if (channel < ARINC_TX_SLOT_COUNT) {
                bool due = s_tx_slots[channel].enabled && ((int32_t)(current_ms - s_tx_slot_due_ms[channel]) >= 0);
                if (s_tx_slots[channel].send_once || due) {
                    tx_slot = s_tx_slots[channel];
                    s_tx_slots[channel].send_once = false;
                    if (s_tx_slots[channel].enabled) {
                        uint16_t period_ms = s_tx_slots[channel].period_ms ? s_tx_slots[channel].period_ms : 50;
                        s_tx_slot_due_ms[channel] = current_ms + period_ms;
                    }
                    s_tx_rr_next = (channel + 1) % (ARINC_TX_SLOT_COUNT + 1);
                    have_tx = true;
                    break;
                }
                continue;
            }

            if (!s_scan_config.enabled || ((int32_t)(current_ms - s_scan_due_ms) < 0)) {
                continue;
            }

            if ((int32_t)(current_ms - s_scan_dwell_due_ms) >= 0) {
                s_scan_label = next_scan_label(s_scan_label, s_scan_config.start_label, s_scan_config.end_label);
                s_scan_dwell_due_ms = current_ms + s_scan_config.dwell_ms;
            }

            uint32_t scan_data = esp_random() & 0x7FFFF;
            uint8_t scan_ssm = 0;
            bool known = find_label_default(s_scan_label, NULL, &scan_ssm);
            if (!known) {
                scan_ssm = s_scan_unknown_ssm & 0x03;
                s_scan_unknown_ssm = (s_scan_unknown_ssm + 1) & 0x03;
            }

            s_scan_sent_count++;
            scan_status = (arinc_scan_status_t){
                .label = s_scan_label,
                .sdi = s_scan_config.sdi,
                .ssm = scan_ssm,
                .data = scan_data,
                .known = known,
                .count = s_scan_sent_count,
            };
            have_scan_status = true;

            tx_slot = (arinc_tx_slot_t){
                .label = s_scan_label,
                .sdi = s_scan_config.sdi,
                .ssm = scan_ssm,
                .data = scan_data,
                .period_ms = s_scan_config.period_ms,
                .offset_ms = 0,
                .enabled = false,
                .send_once = false,
            };
            s_scan_due_ms = current_ms + s_scan_config.period_ms;
            s_tx_rr_next = 0;
            have_tx = true;
            break;
        }
        xSemaphoreGive(arinc_tx_lock);

        if (have_tx) {
            arinc_send_data(tx_slot.label, tx_slot.sdi, tx_slot.data, tx_slot.ssm);
            if (have_scan_status && scan_status_queue) {
                (void)xQueueOverwrite(scan_status_queue, &scan_status);
            }
        }

        vTaskDelay(1);
    }
}
