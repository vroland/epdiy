#include "epd_board_specific.h"

#if defined(CONFIG_EPD_BOARD_REVISION_V6)
#include "pca9555.h"
#include "display_ops.h"

/**
 * Set GPIO direction of the broken-out GPIO extender port.
 * Each pin corresponds to a bit in `direction`.
 * `1` corresponds to input, `0` corresponds to output.
 */
esp_err_t epd_gpio_set_direction(uint8_t direction) {
    return pca9555_set_config(EPDIY_I2C_PORT, direction, 0);
}

/**
 * Get the input level of the broken-out GPIO extender port.
 */
uint8_t epd_gpio_get_level() {
    return pca9555_read_input(EPDIY_I2C_PORT, 0);
}

/**
 * Get the input level of the broken-out GPIO extender port.
 */
esp_err_t epd_gpio_set_value(uint8_t value) {
    return pca9555_set_value(EPDIY_I2C_PORT, value, 0);
}

#endif
