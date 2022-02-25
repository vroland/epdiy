#include "display_ops.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "i2s_data_bus.h"
#include "rmt_pulse.h"
#include "epd_board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "xtensa/core-macros.h"

void IRAM_ATTR busy_delay(uint32_t cycles) {
  volatile unsigned long counts = XTHAL_GET_CCOUNT() + cycles;
  while (XTHAL_GET_CCOUNT() < counts) {
  };
}


void epd_poweron() {
  i2s_gpio_attach();
  epd_board->poweron();
}

#if defined(CONFIG_EPD_BOARD_REVISION_LILYGO_T5_47)
void epd_powerdown() {
  cfg_powerdown(&config_reg);
  i2s_gpio_detach();
}
#endif

void epd_poweroff() {
  epd_board->poweroff();
  i2s_gpio_detach();
}

void epd_deinit() {
  epd_poweroff();
  i2s_deinit();
  epd_board->deinit();

  // FIXME: deinit processes
}

void epd_start_frame() {
  while (i2s_is_busy() || rmt_busy()) {
  };
  // TODO: Remove the start_frame function and use a ctrl interface and put the logic here instead.
  epd_board->start_frame();
}

static inline void latch_row() {
  epd_board->latch_row();
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

  fast_gpio_set_hi(STH);

  latch_row();

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
  // TODO: Use ctrl interface instead and move logic here.
  epd_board->end_frame();
}

void IRAM_ATTR epd_switch_buffer() { i2s_switch_buffer(); }
uint8_t IRAM_ATTR *epd_get_current_buffer() {
  return (uint8_t *)i2s_get_current_buffer();
};
