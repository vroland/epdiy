/**
 * Board-specific functions that are only conditionally defined.
 */

#pragma once

#if defined(CONFIG_EPD_BOARD_REVISION_V6)
#include "pca9555.h"

/**
 * Set GPIO direction of the broken-out GPIO extender port.
 */
static esp_err_t epd_gpio_set_direction(uint8_t direction) {
    pca9555_set_config(EPDIY_I2C_PORT, direction, 0)
}

/**
 * Get the input level of the broken-out GPIO extender port.
 */
static uint8_t epd_gpio_get_level() {
    pca9555_read_input(EPDIY_I2C_PORT, 0)
}

/**
 * Get the input level of the broken-out GPIO extender port.
 */
static uint8_t epd_gpio_set_value(uint8_t value) {
    pca9555_set_value(EPDIY_I2C_PORT, value, 0)
}

#endif
