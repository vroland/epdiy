#include "../output_common/render_method.h"
#include "esp_intr_alloc.h"

#ifdef RENDER_METHOD_I2S

#include "rmt_pulse.h"
#include "../output_common/rmt_compat.h"

static intr_handle_t gRMT_intr_handle = NULL;

static volatile bool rmt_tx_done = true;

static void IRAM_ATTR rmt_interrupt_handler(void* arg) {
    (void)arg;
    rmt_tx_done = true;
    rmt_compat_clear_interrupts();
}

void rmt_pulse_init(gpio_num_t pin) {
    rmt_compat_enable_clock(RMT_COMPAT_CHANNEL_1);
    rmt_compat_connect_gpio(RMT_COMPAT_CHANNEL_1, pin);
    rmt_compat_set_group_clock_src(RMT_COMPAT_CHANNEL_1);
    rmt_compat_set_clock_div(RMT_COMPAT_CHANNEL_1, 8);
    rmt_compat_set_mem_blocks(RMT_COMPAT_CHANNEL_1, 2);
    rmt_compat_enable_mem_access_nonfifo(true);
    rmt_compat_tx_enable_loop(RMT_COMPAT_CHANNEL_1, false);
    rmt_compat_tx_enable_carrier(RMT_COMPAT_CHANNEL_1, false);
    rmt_compat_tx_set_idle_level(RMT_COMPAT_CHANNEL_1, 0, true);

    esp_intr_alloc(
        rmt_compat_get_irq_source(),
        ESP_INTR_FLAG_LEVEL3,
        rmt_interrupt_handler,
        0,
        &gRMT_intr_handle
    );
    rmt_compat_tx_enable_interrupt(RMT_COMPAT_CHANNEL_1, true);
}

void rmt_pulse_deinit() {
    if (gRMT_intr_handle != NULL) {
        esp_intr_disable(gRMT_intr_handle);
        esp_intr_free(gRMT_intr_handle);
        gRMT_intr_handle = NULL;
    }
    rmt_compat_tx_enable_interrupt(RMT_COMPAT_CHANNEL_1, false);
    rmt_compat_disable_clock(RMT_COMPAT_CHANNEL_1);
}

void IRAM_ATTR pulse_ckv_ticks(uint16_t high_time_ticks, uint16_t low_time_ticks, bool wait) {
    while (!rmt_tx_done) {
    };

    if (high_time_ticks > 0) {
        rmt_compat_write_single_item(
            RMT_COMPAT_CHANNEL_1, high_time_ticks, true, low_time_ticks, false, true
        );
    } else {
        rmt_compat_write_single_item(RMT_COMPAT_CHANNEL_1, low_time_ticks, true, 0, false, true);
    }

    rmt_tx_done = false;
    rmt_compat_tx_start_pulse(RMT_COMPAT_CHANNEL_1);

    if (wait) {
        while (!rmt_tx_done) {
        };
    }
}

void IRAM_ATTR pulse_ckv_us(uint16_t high_time_us, uint16_t low_time_us, bool wait) {
    pulse_ckv_ticks(10 * high_time_us, 10 * low_time_us, wait);
}

bool IRAM_ATTR rmt_busy() {
    return !rmt_tx_done;
}

#endif
