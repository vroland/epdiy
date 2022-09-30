#include "epd_board.h"
#include <stdint.h>
//////////////////#include "../include/board/epd_board_v6.h"

#include "esp_log.h"
#include "../s3_lcd.h"

#include <driver/i2c.h>

typedef struct {
    bool pwrup;
    bool vcom_ctrl;
    bool wakeup;
    bool others[8];
} epd_config_register_t;


static void epd_board_init(uint32_t epd_row_width) {
    epd_lcd_init();
}

static void epd_board_deinit() {}

static void epd_board_set_ctrl(epd_ctrl_state_t *state, const epd_ctrl_state_t * const mask) {
}

void epd_poweron() {
}

void epd_poweroff() {
}

static void epd_board_poweron(epd_ctrl_state_t *state) {}

static void epd_board_poweroff(epd_ctrl_state_t *state) {}

static float epd_board_ambient_temperature() {
  return 20;
}

/**
 * Set GPIO direction of the broken-out GPIO extender port.
 * Each pin corresponds to a bit in `direction`.
 * `1` corresponds to input, `0` corresponds to output.
 */
esp_err_t epd_gpio_set_direction_v6(uint8_t direction) {
    return 0;
}

/**
 * Get the input level of the broken-out GPIO extender port.
 */
uint8_t epd_gpio_get_level_v6() {
    return 0;
}

/**
 * Get the input level of the broken-out GPIO extender port.
 */
esp_err_t epd_gpio_set_value_v6(uint8_t value) {
    return 0;
}

uint16_t __attribute__((weak)) epd_board_vcom_v6() {
#ifdef CONFIG_EPD_DRIVER_V6_VCOM
  return CONFIG_EPD_DRIVER_V6_VCOM;
#else
  // Arduino IDE...
  extern int epd_driver_v6_vcom;
  return epd_driver_v6_vcom;
#endif
}

const EpdBoardDefinition epd_board_s3_prototype = {
  .init = epd_board_init,
  .deinit = epd_board_deinit,
  .set_ctrl = epd_board_set_ctrl,
  .poweron = epd_board_poweron,
  .poweroff = epd_board_poweroff,

  .temperature_init = NULL,
  .ambient_temperature = epd_board_ambient_temperature,
};
