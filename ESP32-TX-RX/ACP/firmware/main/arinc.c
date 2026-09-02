#include "arinc.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "ARINC_GPIO_RX";

/* ======================= STATE ======================= */
static volatile uint32_t s_acc = 0;
static volatile uint32_t s_nbits = 0;
static volatile int64_t  s_last_edge_us = 0;

#define ARINC_LABEL_COUNT 256
#define ARINC_SDI_COUNT     4
#define ARINC_KEY_COUNT  (ARINC_LABEL_COUNT * ARINC_SDI_COUNT)

typedef struct {
    bool     valid;
    uint32_t last_norm;
} arinc_last_t;

/* Holds last value for each (label, SDI) */
static arinc_last_t g_last[ARINC_KEY_COUNT];

/* ------------------ ARINC TX ------------------ */
static rmt_channel_handle_t clk_chan = NULL;
static rmt_channel_handle_t mosi_chan = NULL;
static rmt_encoder_handle_t clk_enc  = NULL;
static rmt_encoder_handle_t mosi_enc = NULL;

static rmt_symbol_word_t clk_items[32];
static rmt_symbol_word_t mosi_items[32];
static rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
static rmt_sync_manager_handle_t sync_mgr = NULL;

static volatile uint32_t data19;
static uint32_t rx_word;

static QueueHandle_t arinc_rx_queue_raw;
static QueueHandle_t arinc_rx_queue_raw2;
static QueueHandle_t filtered_queue;
static QueueHandle_t filtered_queue_udp;

/* ======================= UTIL ======================= */
static inline void print_bits_simple(uint32_t w)
{
    char s[WORD_BITS + 1];
    for (int i = WORD_BITS - 1; i >= 0; --i) {
        s[WORD_BITS - 1 - i] = ((w >> i) & 1) ? '1' : '0';
    }
    s[WORD_BITS] = '\0';
    ESP_LOGI(TAG, "bits: %s", s);
}

static inline void print_bits(uint32_t w)
{
    uint8_t  label   = (uint8_t)((w >> 24) & 0xFF);      // [31:24]
    uint8_t  sdi     = (uint8_t)((w >> 22) & 0x03);      // [23:22]
    uint32_t data19  = (uint32_t)((w >> 3)  & 0x7FFFF);  // [21:3]
    uint8_t  ssm     = (uint8_t)((w >> 1)   & 0x03);     // [2:1]
    int parity_ok = (__builtin_popcount(w) & 1) == 1;    // odd parity

    ESP_LOGI(TAG, "ARINC: label=%03o  SDI=%u  DATA19=0x%05X (%d)  SSM=%u  PAR=%s",
             label, sdi, data19, data19, ssm, parity_ok ? "OK" : "BAD");
}

/* ===================== ARINC TX ==================== */
static unsigned char ARINC429_Permute8Bits(unsigned char X)
{
    unsigned char result = 0;

    if (X & 0x80) result |= 0x01;
    if (X & 0x40) result |= 0x02;
    if (X & 0x20) result |= 0x04;
    if (X & 0x10) result |= 0x08;
    if (X & 0x08) result |= 0x10;
    if (X & 0x04) result |= 0x20;
    if (X & 0x02) result |= 0x40;
    if (X & 0x01) result |= 0x80;

    return result;
}

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

static inline void SEND_ARINC429_DATA(uint8_t label, uint8_t sdi, uint32_t data19_local, uint8_t ssm)
{
    data19_local = reverse_bits(data19_local, 19, 0, 0);

    uint32_t w31 = ((uint32_t)label  << 24)
                 | ((uint32_t)sdi    << 22)
                 | ((uint32_t)data19_local << 3)
                 | ((uint32_t)ssm    << 1);

    uint32_t w32 = (w31 & 0xFFFFFFFEUL) | ((uint32_t)ARINC429_ComputeMSBParity(w31));

    for (int i = 0; i < 32; i++) {
        int bit = (w32 >> (31 - i)) & 0x1;
        mosi_items[i].level0 = bit;
        mosi_items[i].duration0 = HALF_PERIOD_US;
        mosi_items[i].level1 = bit;
        mosi_items[i].duration1 = HALF_PERIOD_US;
    }

    rmt_sync_reset(sync_mgr);
    ESP_ERROR_CHECK(rmt_transmit(clk_chan,  clk_enc,  clk_items,  sizeof(clk_items),  &tx_cfg));
    ESP_ERROR_CHECK(rmt_transmit(mosi_chan, mosi_enc, mosi_items, sizeof(mosi_items), &tx_cfg));
}

/* =================== ARINC QUEUES ======================= */
bool arinc_queues_init(void)
{
    arinc_rx_queue_raw  = xQueueCreate(16, sizeof(uint32_t));
    arinc_rx_queue_raw2 = xQueueCreate(16, sizeof(uint32_t));
    filtered_queue      = xQueueCreate(32, sizeof(arinc_msg_t));
    filtered_queue_udp  = xQueueCreate(32, sizeof(arinc_msg_t));

    return arinc_rx_queue_raw && arinc_rx_queue_raw2 && filtered_queue && filtered_queue_udp;
}

QueueHandle_t arinc_get_filtered_queue(void)
{
    return filtered_queue;
}

QueueHandle_t arinc_get_filtered_queue_udp(void)
{
    return filtered_queue_udp;
}

/* =================== ARINC RX ISR ======================= */
void IRAM_ATTR arinc_sclk_isr(void *arg)
{
    (void)arg;
    int64_t now = esp_timer_get_time();
    int64_t dt  = now - s_last_edge_us;
    s_last_edge_us = now;

    if (dt > RESYNC_GAP_US) {
        s_acc = 0;
        s_nbits = 0;
    }

    uint32_t bit = gpio_get_level(PIN_DATA_RX);

    s_acc = (s_acc << 1) | bit;

    if (++s_nbits == WORD_BITS) {
        uint32_t w = s_acc;
        s_acc = 0;
        s_nbits = 0;
        rx_word = w;
        BaseType_t hpw = pdFALSE;
        xQueueSendFromISR(arinc_rx_queue_raw,  &rx_word, &hpw);
        xQueueSendFromISR(arinc_rx_queue_raw2, &rx_word, &hpw);
        if (hpw) portYIELD_FROM_ISR();
    }
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

    for (int i = 0; i < 32; i++) {
        clk_items[i].level0 = 0;
        clk_items[i].duration0 = HALF_PERIOD_US;
        clk_items[i].level1 = 1;
        clk_items[i].duration1 = HALF_PERIOD_US;
    }
}

/* ======================= ARINC RX INIT ======================= */
void arinc_rx_hw_init(void)
{
    gpio_config_t rx_cfg = {
        .pin_bit_mask = (1ULL << PIN_SCLK_RX) | (1ULL << PIN_DATA_RX),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&rx_cfg));

    gpio_set_intr_type(PIN_SCLK_RX, GPIO_INTR_POSEDGE);

    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_SCLK_RX, arinc_sclk_isr, NULL));
}

/* ======================= ARINC TX TASK =========================== */
void arinc_tx_task(void *arg)
{
    QueueHandle_t udp_bits_queue = (QueueHandle_t)arg;
    uint32_t calls = 0;
    uint8_t  voice = 0;

    while (1) {
        uint32_t item;
        uint8_t  reset;

        if (udp_bits_queue) {
            while (xQueueReceive(udp_bits_queue, &calls, 0) == pdTRUE) {
            }
        }

        if (xQueueReceive(arinc_rx_queue_raw2, &item, portMAX_DELAY)) {
            uint8_t  label = (item >> 24) & 0xFF;

            if (label == 0300) {
                SEND_ARINC429_DATA(0301, 0, data19, voice);
            } else {
                //PIN PROG  => 6=VHF3 PRES, 7=HF1 PRES, 8=HF2 PRES, 9=ADF PRES
                //CALLS BIT => 4=MECH 5=ATT, 14=VHF1, 15=VHF2, 16=VHF3, 17=HF1, 18=HF2
                reset = (item >> 5) & 0x1;
                voice = ((item >> 6) & 0x1) << 1; // VOICE ON is on bit30 so goes to SSM

                if (reset == 1) {
                    calls = 0;
                }
                data19 = ARINC429_Permute8Bits((item >> 18) & 0x0F)>>4; //ACP TX CHANNELS ENABLE
                data19 |= (calls>>5)<<4; //CALLS MECH, ATT
                data19 |= (1U << 6) | (1U << 7) | (1U << 8) | (1U << 9) | (1U << 10) | (1U << 11); //PIN PROGS (All ON)
                data19 |= calls<<14;     //CALLS VHF1...HF2
            }
        }
    }
}

/* ================= ARINC RX FILTERING TASK ======================= */
void arinc_filter_task(void *arg)
{
    (void)arg;
    uint32_t word;

    while (1) {
        if (xQueueReceive(arinc_rx_queue_raw, &word, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        uint8_t  label   = (uint8_t)((word >> 24) & 0xFF);      // [31:24]
        uint8_t  sdi     = (uint8_t)((word >> 22) & 0x03);      // [23:22]
        uint32_t data19_local  = (uint32_t)((word >> 3)  & 0x7FFFF);  // [21:3]
        uint8_t  ssm     = (uint8_t)((word >> 1)   & 0x03);     // [2:1]

        uint16_t k = ((uint16_t)label << 2) | (sdi & 0x03);

        uint32_t norm = (data19_local & 0x7FFFF) | ((uint32_t)(ssm & 0x03) << 19);

        bool changed = (!g_last[k].valid) || (norm != g_last[k].last_norm);

        if (changed) {
            g_last[k].valid = true;
            g_last[k].last_norm = norm;

            print_bits(word);

            arinc_msg_t msg = {
                .label = label,
                .sdi   = sdi,
                .ssm   = ssm,
                .data  = data19_local,
                .word  = word
            };

            (void)xQueueSend(filtered_queue,     &msg, 0);
            (void)xQueueSend(filtered_queue_udp, &msg, 0);
        }
    }
}
