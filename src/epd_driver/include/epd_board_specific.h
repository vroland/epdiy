/**
 * @file "epd_board_specific.h"
 * @brief Board-specific functions that are only conditionally defined.
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

/**
 * Set GPIO direction of the broken-out GPIO extender port on v6 boards.
 */
esp_err_t epd_gpio_set_direction_v6(uint8_t direction);
esp_err_t epd_gpio_set_direction(uint8_t direction) __attribute__ ((deprecated));

/**
 * Get the input level of the broken-out GPIO extender port on v6 boards.
 */
uint8_t epd_gpio_get_level_v6();
uint8_t epd_gpio_get_level() __attribute__ ((deprecated));

/**
 * Get the input level of the broken-out GPIO extender port on v6 boards.
 */
esp_err_t epd_gpio_set_value_v6(uint8_t value);
esp_err_t epd_gpio_set_value(uint8_t value) __attribute__ ((deprecated));
