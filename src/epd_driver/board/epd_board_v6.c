#include "epd_board.h"

#include "esp_log.h"
#include "driver/rtc_io.h"
#include "../display_ops.h"
#include "../i2s_data_bus.h"
#include "../rmt_pulse.h"

#define CFG_SCL             GPIO_NUM_33
#define CFG_SDA             GPIO_NUM_32
#define CFG_INTR            GPIO_NUM_35
#define EPDIY_I2C_PORT      I2C_NUM_0

#include "../config_reg_v6.h"

static epd_config_register_t config_reg;

static void epd_board_init(uint32_t epd_row_width) {
  i2c_config_t conf;
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = CFG_SDA;
  conf.scl_io_num = CFG_SCL;
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.master.clk_speed = 400000;
  conf.clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL;
  ESP_ERROR_CHECK(i2c_param_config(EPDIY_I2C_PORT, &conf));

  ESP_ERROR_CHECK(i2c_driver_install(EPDIY_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));

  config_reg.port = EPDIY_I2C_PORT;

  config_reg_init(&config_reg);

  // use latch pin as GPIO
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[V4_LATCH_ENABLE], PIN_FUNC_GPIO);
  ESP_ERROR_CHECK(gpio_set_direction(V4_LATCH_ENABLE, GPIO_MODE_OUTPUT));
  gpio_set_level(V4_LATCH_ENABLE, 0);

  push_cfg(&config_reg);

  // Setup I2S
  // add an offset off dummy bytes to allow for enough timing headroom
  i2s_bus_init( epd_row_width + 32 );

  rmt_pulse_init(CKV);
}

static void epd_board_deinit() {
  gpio_reset_pin(CKH);
  rtc_gpio_isolate(CKH);
  //gpio_reset_pin(CFG_INTR);
  //rtc_gpio_isolate(CFG_INTR);

  cfg_deinit(&config_reg);
  i2c_driver_delete(EPDIY_I2C_PORT);
}

static void epd_board_poweron() {
  cfg_poweron(&config_reg);
}

static void epd_board_poweroff() {
  cfg_poweroff(&config_reg);
}

static void epd_board_start_frame() {
  config_reg.ep_mode = true;
  push_cfg(&config_reg);

  pulse_ckv_us(1, 1, true);

  // This is very timing-sensitive!
  config_reg.ep_stv = false;
  push_cfg(&config_reg);
  //busy_delay(240);
  pulse_ckv_us(1000, 100, false);
  config_reg.ep_stv = true;
  push_cfg(&config_reg);
  //pulse_ckv_us(0, 10, true);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);

  config_reg.ep_output_enable = true;
  push_cfg(&config_reg);
}

static void epd_board_latch_row() {
  fast_gpio_set_hi(V4_LATCH_ENABLE);
  fast_gpio_set_lo(V4_LATCH_ENABLE);
}

static void epd_board_end_frame() {
  config_reg.ep_stv = false;
  push_cfg(&config_reg);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
  config_reg.ep_mode = false;
  push_cfg(&config_reg);
  pulse_ckv_us(0, 10, true);
  config_reg.ep_output_enable = false;
  push_cfg(&config_reg);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
}

const EpdBoardDefinition epd_board_v6 = {
  .init = epd_board_init,
  .deinit = epd_board_deinit,
  .poweron = epd_board_poweron,
  .poweroff = epd_board_poweroff,
  .start_frame = epd_board_start_frame,
  .latch_row = epd_board_latch_row,
  .end_frame = epd_board_end_frame,
};
