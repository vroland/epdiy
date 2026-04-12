#include "rmt_compat.h"

#include <esp_idf_version.h>
#include <esp_attr.h>
#include <esp_intr_alloc.h>
#include <esp_private/periph_ctrl.h>
#include <string.h>

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

void rmt_compat_init(rmt_compat_channel_t channel, gpio_num_t gpio) {
    rmt_compat_enable_clock(channel);
    rmt_compat_connect_gpio(channel, gpio);
    rmt_compat_set_clock_div(channel, 8);
    rmt_compat_set_mem_blocks(channel, 2);

    rmt_ll_tx_enable_loop(&RMT, channel, true);
    rmt_ll_tx_enable_carrier_modulation(&RMT, channel, false);
    rmt_ll_tx_fix_idle_level(&RMT, channel, 0, true);
    rmt_ll_enable_mem_access_nonfifo(&RMT, true);

}

void rmt_compat_deinit(rmt_compat_channel_t channel) {
    rmt_compat_tx_enable_interrupt(channel, false);
    rmt_compat_disable_clock(channel);
}

void rmt_compat_enable_clock(rmt_compat_channel_t channel) {
    (void)channel;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    PERIPH_RCC_ATOMIC() {
        rmt_ll_reset_register(0);
        rmt_ll_enable_bus_clock(0, true);
    }
    rmt_ll_enable_group_clock(&RMT, true);
#else
    periph_module_reset(PERIPH_RMT_MODULE);
    periph_module_enable(PERIPH_RMT_MODULE);
    rmt_ll_enable_periph_clock(&RMT, true);
#endif
}

void rmt_compat_disable_clock(rmt_compat_channel_t channel) {
    (void)channel;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    rmt_ll_enable_group_clock(&RMT, false);
    PERIPH_RCC_ATOMIC() {
        rmt_ll_enable_bus_clock(0, false);
    }
#else
    rmt_ll_enable_periph_clock(&RMT, false);
    periph_module_disable(PERIPH_RMT_MODULE);
#endif
}

void rmt_compat_connect_gpio(rmt_compat_channel_t channel, gpio_num_t gpio) {
    gpio_hal_func_sel(&s_gpio_hal, gpio, PIN_FUNC_GPIO);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    esp_rom_gpio_connect_out_signal(
        gpio, soc_rmt_signals[0].channels[channel].tx_sig, false, 0
    );
#else
    esp_rom_gpio_connect_out_signal(
        gpio, rmt_periph_signals.groups[0].channels[channel].tx_sig, false, 0
    );
#endif
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
}

void rmt_compat_set_clock_div(rmt_compat_channel_t channel, uint8_t div) {
    rmt_ll_tx_set_channel_clock_div(&RMT, channel, div);
}

void rmt_compat_set_mem_blocks(rmt_compat_channel_t channel, uint8_t blocks) {
    rmt_ll_tx_set_mem_blocks(&RMT, channel, blocks);
}

void rmt_compat_tx_start(rmt_compat_channel_t channel) {
    rmt_ll_tx_reset_pointer(&RMT, channel);
    rmt_ll_tx_start(&RMT, channel);
}

void rmt_compat_tx_stop(rmt_compat_channel_t channel) {
    rmt_ll_tx_stop(&RMT, channel);
}

void rmt_compat_tx_reset_mem(rmt_compat_channel_t channel) {
    rmt_ll_tx_reset_pointer(&RMT, channel);
}

void rmt_compat_tx_set_loop(rmt_compat_channel_t channel, bool enable, uint32_t count) {
    rmt_ll_tx_enable_loop(&RMT, channel, enable);
    if (enable) {
        rmt_ll_tx_enable_loop_autostop(&RMT, channel, true);
        rmt_ll_tx_set_loop_count(&RMT, channel, count);
    } else {
        rmt_ll_tx_enable_loop_autostop(&RMT, channel, false);
    }
}

void rmt_compat_tx_enable_loop_count(rmt_compat_channel_t channel, bool enable) {
    rmt_ll_tx_enable_loop_count(&RMT, channel, enable);
}

void rmt_compat_tx_set_loop_count(rmt_compat_channel_t channel, uint32_t count) {
    rmt_ll_tx_set_loop_count(&RMT, channel, count);
}

void rmt_compat_tx_enable_interrupt(rmt_compat_channel_t channel, bool enable) {
    const uint32_t tx_end_bit = 1u << (channel * 3);
    if (enable) {
        RMT.int_ena.val |= tx_end_bit;
    } else {
        RMT.int_ena.val &= ~tx_end_bit;
    }
}

void rmt_compat_tx_prepare(rmt_compat_channel_t channel) {
    rmt_ll_tx_reset_pointer(&RMT, channel);
    rmt_ll_rx_set_mem_owner(&RMT, channel, RMT_LL_MEM_OWNER_HW);
}

void* rmt_compat_get_mem_ptr(rmt_compat_channel_t channel) {
    return (void*)&(RMTMEM.chan[channel].data32[0]);
}
