#pragma once

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

/**
 * Start a draw cycle.
 */
void epd_start_frame();

/**
 * End a draw cycle.
 */
void epd_end_frame();

/**
 * Waits until all previously submitted data has been written.
 * Then, the following operations are initiated:
 *
 *  - Previously submitted data is latched to the output register.
 *  - The RMT peripheral is set up to pulse the vertical (gate) driver for
 *  `output_time_dus` / 10 microseconds.
 *  - The I2S peripheral starts transmission of the current buffer to
 *  the source driver.
 *  - The line buffers are switched.
 *
 * This sequence of operations allows for pipelining data preparation and
 * transfer, reducing total refresh times.
 */
void epd_output_row(uint32_t output_time_dus);

/** Skip a row without writing to it. */
void epd_skip();

/**
 * Get the currently writable line buffer.
 */
uint8_t *epd_get_current_buffer();

/**
 * Switches front and back line buffer.
 * If the switched-to line buffer is currently in use,
 * this function blocks until transmission is done.
 */
void epd_switch_buffer();
