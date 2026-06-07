#include "rmt_compat.h"

#include <esp_idf_version.h>
#include <esp_private/periph_ctrl.h>

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include <driver/gpio.h>
#include <esp_rom_gpio.h>
#include <hal/gpio_hal.h>
#include <hal/rmt_ll.h>
#include <soc/rmt_reg.h>
#include <soc/rmt_struct.h>

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
#include <hal/rmt_periph.h>
#ifndef SOC_RMT_MEM_WORDS_PER_CHANNEL
#define SOC_RMT_MEM_WORDS_PER_CHANNEL 48
#endif

typedef struct {
    struct {
        uint32_t data32[SOC_RMT_MEM_WORDS_PER_CHANNEL];
    } chan[RMT_LL_CHANS_PER_INST];
} rmt_mem_block_t;

extern rmt_mem_block_t RMTMEM;
#else
#include <driver/rmt_types_legacy.h>
#include <soc/rmt_periph.h>
typedef rmt_mem_t rmt_mem_block_t;
extern rmt_mem_block_t RMTMEM;
#endif

#endif  // ESP_IDF_VERSION >= 5.0.0

static gpio_hal_context_t s_gpio_hal = { .dev = GPIO_HAL_GET_HW(GPIO_PORT_0) };

static void rmt_compat_set_module_enabled(bool enable) {
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
    if (enable) {
        periph_module_enable(PERIPH_RMT_MODULE);
    } else {
        periph_module_disable(PERIPH_RMT_MODULE);
    }
#else
    PERIPH_RCC_ATOMIC() {
        rmt_ll_enable_bus_clock(0, enable);
    }
#endif
}

static void rmt_compat_set_periph_clock_enabled(bool enable) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    rmt_ll_enable_group_clock(&RMT, enable);
#else
    rmt_ll_enable_periph_clock(&RMT, enable);
#endif
}

static void rmt_compat_reset_module_regs(void) {
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
    periph_module_reset(PERIPH_RMT_MODULE);
#else
    PERIPH_RCC_ATOMIC() {
        rmt_ll_reset_register(0);
    }
#endif
}

void rmt_compat_enable_clock(rmt_compat_channel_t channel) {
    (void)channel;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    PERIPH_RCC_ATOMIC() {
        rmt_ll_reset_register(0);
    }
    rmt_compat_set_module_enabled(true);
#else
    rmt_compat_reset_module_regs();
    rmt_compat_set_module_enabled(true);
#endif
    rmt_compat_set_periph_clock_enabled(true);
}

void rmt_compat_disable_clock(rmt_compat_channel_t channel) {
    (void)channel;
    rmt_compat_set_periph_clock_enabled(false);
    rmt_compat_set_module_enabled(false);
}

void rmt_compat_enable_periph_clock(bool enable) {
    rmt_compat_set_periph_clock_enabled(enable);
}

void rmt_compat_reset_module(void) {
    rmt_compat_reset_module_regs();
}

void rmt_compat_enable_module(bool enable) {
    rmt_compat_set_module_enabled(enable);
}

void rmt_compat_connect_gpio(rmt_compat_channel_t channel, gpio_num_t gpio) {
    gpio_hal_func_sel(&s_gpio_hal, gpio, PIN_FUNC_GPIO);
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    esp_rom_gpio_connect_out_signal(gpio, soc_rmt_signals[0].channels[channel].tx_sig, false, 0);
#else
    esp_rom_gpio_connect_out_signal(
        gpio, rmt_periph_signals.groups[0].channels[channel].tx_sig, false, 0
    );
#endif
}

void rmt_compat_set_group_clock_src(rmt_compat_channel_t channel) {
    rmt_ll_set_group_clock_src(&RMT, channel, RMT_CLK_SRC_DEFAULT, 1, 0, 0);
}

void rmt_compat_set_clock_div(rmt_compat_channel_t channel, uint8_t div) {
    rmt_ll_tx_set_channel_clock_div(&RMT, channel, div);
}

void rmt_compat_set_mem_blocks(rmt_compat_channel_t channel, uint8_t blocks) {
    rmt_ll_tx_set_mem_blocks(&RMT, channel, blocks);
}

void rmt_compat_enable_mem_access_nonfifo(bool enable) {
    rmt_ll_enable_mem_access_nonfifo(&RMT, enable);
}

void rmt_compat_tx_set_idle_level(rmt_compat_channel_t channel, uint8_t level, bool enable) {
    rmt_ll_tx_fix_idle_level(&RMT, channel, level, enable);
}

void rmt_compat_tx_enable_carrier(rmt_compat_channel_t channel, bool enable) {
    rmt_ll_tx_enable_carrier_modulation(&RMT, channel, enable);
}

void rmt_compat_tx_enable_loop(rmt_compat_channel_t channel, bool enable) {
    rmt_ll_tx_enable_loop(&RMT, channel, enable);
}

void rmt_compat_tx_start(rmt_compat_channel_t channel) {
    rmt_ll_tx_reset_pointer(&RMT, channel);
    rmt_ll_tx_start(&RMT, channel);
}

void rmt_compat_tx_reset_mem(rmt_compat_channel_t channel) {
    rmt_ll_tx_reset_pointer(&RMT, channel);
}

void rmt_compat_tx_configure_finite_loop(rmt_compat_channel_t channel, uint32_t count) {
#if defined(SOC_RMT_SUPPORT_TX_LOOP_COUNT) && SOC_RMT_SUPPORT_TX_LOOP_COUNT
    rmt_ll_tx_enable_loop_count(&RMT, channel, true);
#if defined(SOC_RMT_SUPPORT_TX_LOOP_AUTO_STOP) && SOC_RMT_SUPPORT_TX_LOOP_AUTO_STOP
    rmt_ll_tx_enable_loop_autostop(&RMT, channel, true);
#endif
    rmt_ll_tx_set_loop_count(&RMT, channel, count);
#else
    (void)channel;
    (void)count;
#endif
}

int rmt_compat_get_irq_source(void) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    return soc_rmt_signals[0].irq;
#else
    return rmt_periph_signals.groups[0].irq;
#endif
}

void rmt_compat_tx_enable_interrupt(rmt_compat_channel_t channel, bool enable) {
    const uint32_t tx_end_bit = 1u << (channel * 3);
    if (enable) {
        RMT.int_ena.val |= tx_end_bit;
    } else {
        RMT.int_ena.val &= ~tx_end_bit;
    }
}

static void rmt_compat_tx_set_mem_owner(rmt_compat_channel_t channel) {
    // Mirror the legacy pulse path by handing RAM ownership back to TX/APB before start.
    rmt_ll_rx_set_mem_owner(&RMT, channel, RMT_LL_MEM_OWNER_SW);
}

void rmt_compat_tx_start_pulse(rmt_compat_channel_t channel) {
    rmt_compat_tx_reset_mem(channel);
    rmt_compat_tx_set_mem_owner(channel);
    rmt_compat_tx_start(channel);
}

void rmt_compat_clear_interrupts(void) {
    RMT.int_clr.val = RMT.int_st.val;
}

void rmt_compat_write_single_item(
    rmt_compat_channel_t channel,
    uint16_t duration0,
    bool level0,
    uint16_t duration1,
    bool level1,
    bool terminate
) {
    volatile epdiy_rmt_item_t* rmt_mem_ptr = (void*)&(RMTMEM.chan[channel].data32[0]);

    rmt_mem_ptr[0].duration0 = duration0;
    rmt_mem_ptr[0].level0 = level0;
    rmt_mem_ptr[0].duration1 = duration1;
    rmt_mem_ptr[0].level1 = level1;

    if (terminate) {
        rmt_mem_ptr[1].val = 0;
    }
}
