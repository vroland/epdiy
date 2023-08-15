#include "display_ops.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "i2s_data_bus.h"

#include "epd_board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "xtensa/core-macros.h"

static epd_ctrl_state_t ctrl_state;

void IRAM_ATTR busy_delay(uint32_t cycles) {
  volatile unsigned long counts = XTHAL_GET_CCOUNT() + cycles;
  while (XTHAL_GET_CCOUNT() < counts) {
  };
}

void epd_hw_init(uint32_t epd_row_width) {
  ctrl_state.ep_latch_enable = false;
  ctrl_state.ep_output_enable = false;
  ctrl_state.ep_sth = true;
  ctrl_state.ep_mode = false;
  ctrl_state.ep_stv = true;
  epd_ctrl_state_t mask = {
    .ep_latch_enable = true,
    .ep_output_enable = true,
    .ep_sth = true,
    .ep_mode = true,
    .ep_stv = true,
  };

  epd_board->set_ctrl(&ctrl_state, &mask);
}

void epd_set_mode(bool state) {
  ctrl_state.ep_output_enable = state;
  ctrl_state.ep_mode = state;
  epd_ctrl_state_t mask = {
    .ep_output_enable = true,
    .ep_mode = true,
  };
  epd_board->set_ctrl(&ctrl_state, &mask);
}

void epd_poweron() {
  epd_board->poweron(&ctrl_state);
}

void epd_poweroff() {
  epd_board->poweroff(&ctrl_state);
}

epd_ctrl_state_t *epd_ctrl_state() {
  return &ctrl_state;
}

void epd_deinit() {
  epd_poweroff();
#ifdef RENDER_METHOD_I2S
  i2s_deinit();
#endif

  ctrl_state.ep_output_enable = false;
  ctrl_state.ep_mode = false;
  ctrl_state.ep_stv = false;
  epd_ctrl_state_t mask = {
    .ep_output_enable = true,
    .ep_mode = true,
    .ep_stv = true,
  };
  epd_board->set_ctrl(&ctrl_state, &mask);

  if (epd_board->deinit) {
    epd_board->deinit();
  }

  // FIXME: deinit processes
}
