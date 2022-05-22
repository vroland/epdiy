#include "epd_board_specific.h"

/**
 * Set GPIO direction of the broken-out GPIO extender port.
 * Each pin corresponds to a bit in `direction`.
 * `1` corresponds to input, `0` corresponds to output.
 */
esp_err_t epd_gpio_set_direction(uint8_t direction) {
    return epd_gpio_set_direction_v6(direction);
}

/**
 * Get the input level of the broken-out GPIO extender port.
 */
uint8_t epd_gpio_get_level() {
    return epd_gpio_get_level_v6();
}

/**
 * Get the input level of the broken-out GPIO extender port.
 */
esp_err_t epd_gpio_set_value(uint8_t value) {
    return epd_gpio_set_value_v6(value);
}

void epd_powerdown() {
    epd_powerdown_lilygo_t5_47();
}
