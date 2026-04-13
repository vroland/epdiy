#ifndef EPDIY_RMT_COMPAT_H
#define EPDIY_RMT_COMPAT_H

#include <driver/gpio.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RMT_COMPAT_CHANNEL_0 = 0,
    RMT_COMPAT_CHANNEL_1 = 1,
    RMT_COMPAT_CHANNEL_2 = 2,
    RMT_COMPAT_CHANNEL_3 = 3,
    RMT_COMPAT_CHANNEL_MAX
} rmt_compat_channel_t;

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
} epdiy_rmt_item_t;

void rmt_compat_enable_clock(rmt_compat_channel_t channel);
void rmt_compat_disable_clock(rmt_compat_channel_t channel);

void rmt_compat_connect_gpio(rmt_compat_channel_t channel, gpio_num_t gpio);
void rmt_compat_set_clock_div(rmt_compat_channel_t channel, uint8_t div);
void rmt_compat_set_mem_blocks(rmt_compat_channel_t channel, uint8_t blocks);
void rmt_compat_enable_mem_access_nonfifo(bool enable);
void rmt_compat_tx_set_idle_level(rmt_compat_channel_t channel, uint8_t level, bool enable);
void rmt_compat_tx_enable_carrier(rmt_compat_channel_t channel, bool enable);
void rmt_compat_tx_enable_loop(rmt_compat_channel_t channel, bool enable);

void rmt_compat_tx_start(rmt_compat_channel_t channel);
void rmt_compat_tx_stop(rmt_compat_channel_t channel);
void rmt_compat_tx_reset_mem(rmt_compat_channel_t channel);
void rmt_compat_tx_set_loop(rmt_compat_channel_t channel, bool enable, uint32_t count);
void rmt_compat_tx_enable_loop_count(rmt_compat_channel_t channel, bool enable);
void rmt_compat_tx_set_loop_count(rmt_compat_channel_t channel, uint32_t count);
void rmt_compat_tx_enable_interrupt(rmt_compat_channel_t channel, bool enable);

void* rmt_compat_get_mem_ptr(rmt_compat_channel_t channel);

#ifdef __cplusplus
}
#endif

#endif  // EPDIY_RMT_COMPAT_H
