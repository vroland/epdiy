#pragma once

#include "render_method.h"
#include "driver/gpio.h"
#include "epd_board.h"
#include "esp_attr.h"

#include "esp_system.h"  // for ESP_IDF_VERSION_VAL
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"
#endif

/*
 * Write bits directly using the registers.
 * Won't work for some pins (>= 32).
 */
inline void fast_gpio_set_hi(gpio_num_t gpio_num) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    gpio_dev_t *device = GPIO_LL_GET_HW(GPIO_PORT_0);
    device->out_w1ts   = (1 << gpio_num);
#else
    GPIO.out_w1ts = (1 << gpio_num);
#endif
}

inline void fast_gpio_set_lo(gpio_num_t gpio_num) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    gpio_dev_t *device = GPIO_LL_GET_HW(GPIO_PORT_0);
    device->out_w1tc   = (1 << gpio_num);
#else
    GPIO.out_w1tc = (1 << gpio_num);
#endif
}

void busy_delay(uint32_t cycles);

void epd_hw_init(uint32_t epd_row_width);
void epd_poweron();
void epd_poweroff();

epd_ctrl_state_t *epd_ctrl_state();
void epd_set_mode(bool state);

