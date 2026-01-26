#include "../output_common/render_method.h"
#include "esp_intr_alloc.h"

#ifdef RENDER_METHOD_I2S

#include <esp_idf_version.h>

#include "rmt_pulse.h"

#include "soc/rmt_struct.h"

static intr_handle_t gRMT_intr_handle = NULL;

// keep track of whether the current pulse is ongoing
volatile bool rmt_tx_done = true;

/**
 * Remote peripheral interrupt. Used to signal when transmission is done.
 */
static void IRAM_ATTR rmt_interrupt_handler(void* arg) {
    rmt_tx_done = true;
    RMT.int_clr.val = RMT.int_st.val;
}

// The extern line is declared in esp-idf/components/driver/deprecated/rmt_legacy.c. It has access
// to RMTMEM through the rmt_private.h header which we can't access outside the sdk. Declare our own
// extern here to properly use the RMTMEM symbol defined in
// components/soc/[target]/ld/[target].peripherals.ld Also typedef the new rmt_mem_t struct to the
// old rmt_block_mem_t struct. Same data fields, different names
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
// IDF 6.x: rmt_mem_t and rmt_item32_t removed with legacy driver.
// Define compatible types for direct RMT memory access.
#include <hal/rmt_types.h>
#include <hal/rmt_ll.h>
#include <hal/rmt_periph.h>
#include <soc/rmt_reg.h>
typedef struct {
    union {
        struct {
            uint32_t duration0 : 15;
            uint32_t level0 : 1;
            uint32_t duration1 : 15;
            uint32_t level1 : 1;
        };
        uint32_t val;
    };
} rmt_item32_t;
#ifndef SOC_RMT_MEM_WORDS_PER_CHANNEL
#define SOC_RMT_MEM_WORDS_PER_CHANNEL 48
#endif
typedef struct {
    struct {
        rmt_item32_t data32[SOC_RMT_MEM_WORDS_PER_CHANNEL];
    } chan[RMT_LL_CHANS_PER_INST];
} rmt_block_mem_t;
extern rmt_block_mem_t RMTMEM;

// Channel index used for RMT pulse generation
#define RMT_PULSE_CHANNEL 1
#define RMT_MEM_OWNER_TX 1

#include <esp_private/periph_ctrl.h>
#include <driver/gpio.h>
#include "esp_rom_gpio.h"

void rmt_pulse_init(gpio_num_t pin) {
    // Enable RMT peripheral clock via LL (PERIPH_RMT_MODULE removed in IDF 6)
    PERIPH_RCC_ATOMIC() {
        rmt_ll_reset_register(0);
        rmt_ll_enable_bus_clock(0, true);
    }

    // Configure channel via direct register access (legacy rmt_config() removed in IDF 6)
    // Set clock divider: 80MHz / 8 = 10MHz -> 0.1us resolution
    RMT.conf_ch[RMT_PULSE_CHANNEL].conf0.div_cnt = 8;
    RMT.conf_ch[RMT_PULSE_CHANNEL].conf0.mem_size = 2;
    RMT.conf_ch[RMT_PULSE_CHANNEL].conf0.carrier_en = 0;
    RMT.conf_ch[RMT_PULSE_CHANNEL].conf0.carrier_out_lv = 0;
    RMT.conf_ch[RMT_PULSE_CHANNEL].conf1.tx_conti_mode = 0;  // loop_en = false
    RMT.conf_ch[RMT_PULSE_CHANNEL].conf1.idle_out_en = 1;
    RMT.conf_ch[RMT_PULSE_CHANNEL].conf1.idle_out_lv = 0;  // idle level low

    // Connect GPIO to RMT channel
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal(
        pin, soc_rmt_signals[0].channels[RMT_PULSE_CHANNEL].tx_sig, false, false
    );

    // Allocate interrupt
    esp_intr_alloc(
        ETS_RMT_INTR_SOURCE, ESP_INTR_FLAG_LEVEL3, rmt_interrupt_handler, 0, &gRMT_intr_handle
    );

    // Enable TX done interrupt for this channel
    RMT.int_ena.val |= (1 << (RMT_PULSE_CHANNEL * 3));  // TX_END interrupt bit
}

void rmt_pulse_deinit() {
    esp_intr_disable(gRMT_intr_handle);
    esp_intr_free(gRMT_intr_handle);
}

void IRAM_ATTR pulse_ckv_ticks(uint16_t high_time_ticks, uint16_t low_time_ticks, bool wait) {
    while (!rmt_tx_done) {
    };
    volatile rmt_item32_t* rmt_mem_ptr = &(RMTMEM.chan[RMT_PULSE_CHANNEL].data32[0]);
    if (high_time_ticks > 0) {
        rmt_mem_ptr->level0 = 1;
        rmt_mem_ptr->duration0 = high_time_ticks;
        rmt_mem_ptr->level1 = 0;
        rmt_mem_ptr->duration1 = low_time_ticks;
    } else {
        rmt_mem_ptr->level0 = 1;
        rmt_mem_ptr->duration0 = low_time_ticks;
        rmt_mem_ptr->level1 = 0;
        rmt_mem_ptr->duration1 = 0;
    }
    RMTMEM.chan[RMT_PULSE_CHANNEL].data32[1].val = 0;
    rmt_tx_done = false;
    RMT.conf_ch[RMT_PULSE_CHANNEL].conf1.mem_rd_rst = 1;
    RMT.conf_ch[RMT_PULSE_CHANNEL].conf1.mem_owner = RMT_MEM_OWNER_TX;
    RMT.conf_ch[RMT_PULSE_CHANNEL].conf1.tx_start = 1;
    while (wait && !rmt_tx_done) {
    };
}

#else
// IDF 4.x / 5.x: use legacy RMT driver
#include "driver/rmt.h"

typedef rmt_mem_t rmt_block_mem_t;
extern rmt_block_mem_t RMTMEM;

// the RMT channel configuration object
static rmt_config_t row_rmt_config;

void rmt_pulse_init(gpio_num_t pin) {
    row_rmt_config.rmt_mode = RMT_MODE_TX;
    // currently hardcoded: use channel 0
    row_rmt_config.channel = RMT_CHANNEL_1;

    row_rmt_config.gpio_num = pin;
    row_rmt_config.mem_block_num = 2;

    // Divide 80MHz APB Clock by 8 -> .1us resolution delay
    row_rmt_config.clk_div = 8;

    row_rmt_config.tx_config.loop_en = false;
    row_rmt_config.tx_config.carrier_en = false;
    row_rmt_config.tx_config.carrier_level = RMT_CARRIER_LEVEL_LOW;
    row_rmt_config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    row_rmt_config.tx_config.idle_output_en = true;

    esp_intr_alloc(
        ETS_RMT_INTR_SOURCE, ESP_INTR_FLAG_LEVEL3, rmt_interrupt_handler, 0, &gRMT_intr_handle
    );

    rmt_config(&row_rmt_config);
    rmt_set_tx_intr_en(row_rmt_config.channel, true);
}

void rmt_pulse_deinit() {
    esp_intr_disable(gRMT_intr_handle);
    esp_intr_free(gRMT_intr_handle);
}

void IRAM_ATTR pulse_ckv_ticks(uint16_t high_time_ticks, uint16_t low_time_ticks, bool wait) {
    while (!rmt_tx_done) {
    };
    volatile rmt_item32_t* rmt_mem_ptr = &(RMTMEM.chan[row_rmt_config.channel].data32[0]);
    if (high_time_ticks > 0) {
        rmt_mem_ptr->level0 = 1;
        rmt_mem_ptr->duration0 = high_time_ticks;
        rmt_mem_ptr->level1 = 0;
        rmt_mem_ptr->duration1 = low_time_ticks;
    } else {
        rmt_mem_ptr->level0 = 1;
        rmt_mem_ptr->duration0 = low_time_ticks;
        rmt_mem_ptr->level1 = 0;
        rmt_mem_ptr->duration1 = 0;
    }
    RMTMEM.chan[row_rmt_config.channel].data32[1].val = 0;
    rmt_tx_done = false;
    RMT.conf_ch[row_rmt_config.channel].conf1.mem_rd_rst = 1;
    RMT.conf_ch[row_rmt_config.channel].conf1.mem_owner = RMT_MEM_OWNER_TX;
    RMT.conf_ch[row_rmt_config.channel].conf1.tx_start = 1;
    while (wait && !rmt_tx_done) {
    };
}
#endif

void IRAM_ATTR pulse_ckv_us(uint16_t high_time_us, uint16_t low_time_us, bool wait) {
    pulse_ckv_ticks(10 * high_time_us, 10 * low_time_us, wait);
}

bool IRAM_ATTR rmt_busy() {
    return !rmt_tx_done;
}

#endif
