#include "display_ops.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "i2s_data_bus.h"
#include "rmt_pulse.h"
#include "epd_board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "xtensa/core-macros.h"

static epd_ctrl_state_t ctrl_state;
static const epd_ctrl_state_t NoChangeState = {0};

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
#ifndef RENDER_METHOD_LCD
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

void epd_start_frame() {
  while (i2s_is_busy() || rmt_busy()) {
  };

  epd_ctrl_state_t mask = NoChangeState;

  ctrl_state.ep_mode = true;
  mask.ep_mode = true;
  epd_board->set_ctrl(&ctrl_state, &mask);

  pulse_ckv_us(1, 1, true);

  // This is very timing-sensitive!
  mask = NoChangeState;
  ctrl_state.ep_stv = false;
  mask.ep_stv = true;
  epd_board->set_ctrl(&ctrl_state, &mask);
  //busy_delay(240);
  pulse_ckv_us(1000, 100, false);
  mask = NoChangeState;
  ctrl_state.ep_stv = true;
  mask.ep_stv = true;
  epd_board->set_ctrl(&ctrl_state, &mask);
  //pulse_ckv_us(0, 10, true);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);

  mask = NoChangeState;
  ctrl_state.ep_output_enable = true;
  mask.ep_output_enable = true;
  epd_board->set_ctrl(&ctrl_state, &mask);
}

void IRAM_ATTR epd_skip() {
#if defined(CONFIG_EPD_DISPLAY_TYPE_ED097TC2) ||                               \
    defined(CONFIG_EPD_DISPLAY_TYPE_ED133UT2)
  pulse_ckv_ticks(5, 5, false);
#else
  // According to the spec, the OC4 maximum CKV frequency is 200kHz.
  pulse_ckv_ticks(45, 5, false);
#endif
}

void IRAM_ATTR epd_output_row(uint32_t output_time_dus) {
  while (i2s_is_busy() || rmt_busy()) {
  };

  epd_ctrl_state_t mask = NoChangeState;
  ctrl_state.ep_sth = true;
  ctrl_state.ep_latch_enable = true;
  mask.ep_sth = true;
  mask.ep_latch_enable = true;
  epd_board->set_ctrl(&ctrl_state, &mask);

  mask = NoChangeState;
  ctrl_state.ep_latch_enable = false;
  mask.ep_latch_enable = true;
  epd_board->set_ctrl(&ctrl_state, &mask);

#if defined(CONFIG_EPD_DISPLAY_TYPE_ED097TC2) ||                               \
    defined(CONFIG_EPD_DISPLAY_TYPE_ED133UT2)
  pulse_ckv_ticks(output_time_dus, 1, false);
#else
  pulse_ckv_ticks(output_time_dus, 50, false);
#endif

  i2s_start_line_output();
  i2s_switch_buffer();
}

void epd_end_frame() {
  epd_ctrl_state_t mask = NoChangeState;
  ctrl_state.ep_stv = false;
  mask.ep_stv = true;
  epd_board->set_ctrl(&ctrl_state, &mask);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
  mask = NoChangeState;
  ctrl_state.ep_mode = false;
  mask.ep_mode = true;
  epd_board->set_ctrl(&ctrl_state, &mask);
  pulse_ckv_us(0, 10, true);
  mask = NoChangeState;
  ctrl_state.ep_output_enable = false;
  mask.ep_output_enable = true;
  epd_board->set_ctrl(&ctrl_state, &mask);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
}

void IRAM_ATTR epd_switch_buffer() { i2s_switch_buffer(); }
uint8_t IRAM_ATTR *epd_get_current_buffer() {
  return (uint8_t *)i2s_get_current_buffer();
};
