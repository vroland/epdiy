/**
 * @file "epd_board_specific.h"
 * @brief Board-specific functions that are only conditionally defined.
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

/**
 * Set GPIO direction of the broken-out GPIO extender port.
 */
esp_err_t epd_gpio_set_direction(uint8_t direction);

/**
 * Get the input level of the broken-out GPIO extender port.
 */
uint8_t epd_gpio_get_level();

/**
 * Get the input level of the broken-out GPIO extender port.
 */
esp_err_t epd_gpio_set_value(uint8_t value);
