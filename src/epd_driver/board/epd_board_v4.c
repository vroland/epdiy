#include "epd_board.h"

#include "../display_ops.h"
#include "../i2s_data_bus.h"
#include "../rmt_pulse.h"

#define CFG_DATA GPIO_NUM_23
#define CFG_CLK GPIO_NUM_18
#define CFG_STR GPIO_NUM_19
#define D7 GPIO_NUM_22
#define D6 GPIO_NUM_21
#define D5 GPIO_NUM_27
#define D4 GPIO_NUM_2
#define D3 GPIO_NUM_0
#define D2 GPIO_NUM_4
#define D1 GPIO_NUM_32
#define D0 GPIO_NUM_33

/* Control Lines */
#define CKV GPIO_NUM_25
#define STH GPIO_NUM_26

#define V4_LATCH_ENABLE GPIO_NUM_15

/* Edges */
#define CKH GPIO_NUM_5

typedef struct {
  bool power_disable : 1;
  bool power_enable_vpos : 1;
  bool power_enable_vneg : 1;
  bool power_enable_gl : 1;
  bool ep_stv : 1;
  bool power_enable_gh : 1;
  bool ep_mode : 1;
  bool ep_output_enable : 1;
} epd_config_register_t;

static i2s_bus_config i2s_config = {
  .clock = CKH,
  .start_pulse = STH,
  .data_0 = D0,
  .data_1 = D1,
  .data_2 = D2,
  .data_3 = D3,
  .data_4 = D4,
  .data_5 = D5,
  .data_6 = D6,
  .data_7 = D7,
};

static void IRAM_ATTR push_cfg_bit(bool bit) {
  gpio_set_level(CFG_CLK, 0);
  gpio_set_level(CFG_DATA, bit);
  gpio_set_level(CFG_CLK, 1);
}

static void IRAM_ATTR push_cfg(const epd_config_register_t *cfg) {
  fast_gpio_set_lo(CFG_STR);

  // push config bits in reverse order
  push_cfg_bit(cfg->ep_output_enable);
  push_cfg_bit(cfg->ep_mode);
  push_cfg_bit(cfg->power_enable_gh);
  push_cfg_bit(cfg->ep_stv);

  push_cfg_bit(cfg->power_enable_gl);
  push_cfg_bit(cfg->power_enable_vneg);
  push_cfg_bit(cfg->power_enable_vpos);
  push_cfg_bit(cfg->power_disable);

  fast_gpio_set_hi(CFG_STR);
  fast_gpio_set_lo(CFG_STR);
}

static epd_config_register_t config_reg;

static void epd_board_init(uint32_t epd_row_width) {
  /* Power Control Output/Off */
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[CFG_DATA], PIN_FUNC_GPIO);
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[CFG_CLK], PIN_FUNC_GPIO);
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[CFG_STR], PIN_FUNC_GPIO);
  gpio_set_direction(CFG_DATA, GPIO_MODE_OUTPUT);
  gpio_set_direction(CFG_CLK, GPIO_MODE_OUTPUT);
  gpio_set_direction(CFG_STR, GPIO_MODE_OUTPUT);
  fast_gpio_set_lo(CFG_STR);

  config_reg.power_disable = true;
  config_reg.power_enable_vpos = false;
  config_reg.power_enable_vneg = false;
  config_reg.power_enable_gl = false;
  config_reg.ep_stv = true;
  config_reg.power_enable_gh = false;
  config_reg.ep_mode = false;
  config_reg.ep_output_enable = false;

  // use latch pin as GPIO
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[V4_LATCH_ENABLE], PIN_FUNC_GPIO);
  ESP_ERROR_CHECK(gpio_set_direction(V4_LATCH_ENABLE, GPIO_MODE_OUTPUT));
  gpio_set_level(V4_LATCH_ENABLE, 0);

  push_cfg(&config_reg);

  // Setup I2S
  // add an offset off dummy bytes to allow for enough timing headroom
  i2s_bus_init( &i2s_config, epd_row_width + 32 );

  rmt_pulse_init(CKV);
}

static void epd_board_deinit() {
  config_reg.ep_stv = false;
  config_reg.ep_mode = false;
  config_reg.ep_output_enable = false;
  push_cfg(&config_reg);
}

static void epd_board_poweron() {
  // POWERON
  i2s_gpio_attach(&i2s_config);

  config_reg.power_disable = false;
  push_cfg(&config_reg);
  busy_delay(100 * 240);
  config_reg.power_enable_gl = true;
  push_cfg(&config_reg);
  busy_delay(500 * 240);
  config_reg.power_enable_vneg = true;
  push_cfg(&config_reg);
  busy_delay(500 * 240);
  config_reg.power_enable_gh = true;
  push_cfg(&config_reg);
  busy_delay(500 * 240);
  config_reg.power_enable_vpos = true;
  push_cfg(&config_reg);
  busy_delay(100 * 240);
  config_reg.ep_stv = true;
  push_cfg(&config_reg);
  fast_gpio_set_hi(STH);
  // END POWERON
}

static void epd_board_poweroff() {
  // POWEROFF
  config_reg.power_enable_gh = false;
  config_reg.power_enable_vpos = false;
  push_cfg(&config_reg);
  busy_delay(10 * 240);
  config_reg.power_enable_gl = false;
  config_reg.power_enable_vneg = false;
  push_cfg(&config_reg);
  busy_delay(100 * 240);

  config_reg.ep_stv = false;
  config_reg.ep_output_enable = false;
  config_reg.ep_mode = false;
  config_reg.power_disable = true;
  push_cfg(&config_reg);

  i2s_gpio_detach(&i2s_config);
  // END POWEROFF
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
  fast_gpio_set_hi(STH);
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

const EpdBoardDefinition epd_board_v4 = {
  .init = epd_board_init,
  .deinit = epd_board_deinit,
  .poweron = epd_board_poweron,
  .poweroff = epd_board_poweroff,
  .start_frame = epd_board_start_frame,
  .latch_row = epd_board_latch_row,
  .end_frame = epd_board_end_frame,
};
