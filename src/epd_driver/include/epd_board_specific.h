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

/** This is a Lilygo47 specific function

  This is a work around a hardware issue with the Lilygo47 epd_poweroff() turns off the epaper completely
  however the hardware of the Lilygo47 is different than the official boards. Which means that on the Lilygo47 this
  disables power to the touchscreen.

  This is a workaround to allow to disable display power but not the touch screen.
  On the Lilygo the epd power flag was re-purposed as power enable
  for everything. This is a hardware thing.
 \warning This workaround may still leave power on to epd and as such may cause other problems such as grey screen.
  Please also use epd_poweroff() and epd_deinit() when you sleep the system wake on touch will still work.

 Arduino specific code:
 \code{.c}
  epd_powerdown_lilygo_t5_47();
  epd_deinit();
  esp_sleep_enable_ext1_wakeup(GPIO_SEL_13, ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_deep_sleep_start();
  \endcode
*/
void epd_powerdown_lilygo_t5_47();
void epd_powerdown() __attribute__ ((deprecated));
