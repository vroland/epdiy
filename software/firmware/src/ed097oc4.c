#include "ed097oc4.h"
#include "i2s_data_bus.h"

#include "driver/rmt.h"

typedef struct {
  bool ep_latch_enable : 1;
  bool power_disable : 1;
  bool pos_power_enable : 1;
  bool neg_power_enable : 1;
  bool ep_stv : 1;
  bool ep_scan_direction : 1;
  bool ep_mode : 1;
  bool ep_output_enable : 1;
} epd_config_register_t;

epd_config_register_t config_reg;

static intr_handle_t gRMT_intr_handle = NULL;

// Use a remote control peripheral channel for row timings
rmt_config_t row_rmt_config;
volatile bool rmt_tx_done = true;

inline void gpio_set_hi(gpio_num_t gpio_num) { gpio_set_level(gpio_num, 1); }

inline void gpio_set_lo(gpio_num_t gpio_num) { gpio_set_level(gpio_num, 0); }

/*
Write bits directly using the registers.  Won't work for some signals
(>= 32). May be too fast for some signals.
*/
inline void fast_gpio_set_hi(gpio_num_t gpio_num) {
  GPIO.out_w1ts = (1 << gpio_num);
}

inline void fast_gpio_set_lo(gpio_num_t gpio_num) {

  GPIO.out_w1tc = (1 << gpio_num);
}

void IRAM_ATTR busy_delay(uint32_t cycles) {
  volatile unsigned long counts = xthal_get_ccount() + cycles;
  while (xthal_get_ccount() < counts) {
  };
}

void IRAM_ATTR push_cfg_bit(bool bit) {
  fast_gpio_set_lo(CFG_CLK);
  if (bit) {
    fast_gpio_set_hi(CFG_DATA);
  } else {
    fast_gpio_set_lo(CFG_DATA);
  }
  fast_gpio_set_hi(CFG_CLK);
}

void IRAM_ATTR push_cfg(epd_config_register_t *cfg) {
  fast_gpio_set_lo(CFG_STR);

  // push config bits in reverse order
  push_cfg_bit(cfg->ep_output_enable);
  push_cfg_bit(cfg->ep_mode);
  push_cfg_bit(cfg->ep_scan_direction);
  push_cfg_bit(cfg->ep_stv);

  push_cfg_bit(cfg->neg_power_enable);
  push_cfg_bit(cfg->pos_power_enable);
  push_cfg_bit(cfg->power_disable);
  push_cfg_bit(cfg->ep_latch_enable);

  fast_gpio_set_hi(CFG_STR);
}

// -- Custom interrupt handler
// Signal when the RMT is done.
static void IRAM_ATTR rmt_interrupt_handler(void *arg) {
  // -- The basic structure of this code is borrowed from the
  //    interrupt handler in esp-idf/components/driver/rmt.c
  rmt_tx_done = true;
  RMT.int_clr.val = RMT.int_st.val;
}

void init_gpios() {

  config_reg.ep_latch_enable = false;
  config_reg.power_disable = true;
  config_reg.pos_power_enable = false;
  config_reg.neg_power_enable = false;
  config_reg.ep_stv = true;
  config_reg.ep_scan_direction = true;
  config_reg.ep_mode = false;
  config_reg.ep_output_enable = false;

  /* Power Control Output/Off */
  gpio_set_direction(CFG_DATA, GPIO_MODE_OUTPUT);
  gpio_set_direction(CFG_CLK, GPIO_MODE_OUTPUT);
  gpio_set_direction(CFG_STR, GPIO_MODE_OUTPUT);
  gpio_set_lo(CFG_STR);

  push_cfg(&config_reg);

  gpio_set_direction(CKV, GPIO_MODE_OUTPUT);
  gpio_set_lo(CKV);

  // Setup I2S
  i2s_bus_config i2s_config;
  i2s_config.epd_row_width = EPD_WIDTH;
  i2s_config.clock = CKH;
  i2s_config.start_pulse = STH;
  i2s_config.data_0 = D0;
  i2s_config.data_1 = D1;
  i2s_config.data_2 = D2;
  i2s_config.data_3 = D3;
  i2s_config.data_4 = D4;
  i2s_config.data_5 = D5;
  i2s_config.data_6 = D6;
  i2s_config.data_7 = D7;

  i2s_bus_init(&i2s_config);

  // Setup RMT peripheral
  row_rmt_config.rmt_mode = RMT_MODE_TX;
  row_rmt_config.channel = RMT_CHANNEL_0;
  row_rmt_config.gpio_num = CKV;
  row_rmt_config.mem_block_num = 1;
  row_rmt_config.clk_div = 80; // Divide 80MHz by 80 -> 1us delay

  row_rmt_config.tx_config.loop_en = false;
  row_rmt_config.tx_config.carrier_en = false;
  row_rmt_config.tx_config.carrier_level = RMT_CARRIER_LEVEL_LOW;
  row_rmt_config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
  row_rmt_config.tx_config.idle_output_en = true;

  esp_intr_alloc(ETS_RMT_INTR_SOURCE, ESP_INTR_FLAG_LEVEL3,
                 rmt_interrupt_handler, 0, &gRMT_intr_handle);
  heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);

  rmt_config(&row_rmt_config);
  rmt_set_tx_intr_en(row_rmt_config.channel, true);
}

/**
 * Outputs a high pulse on CKV signal for a given number of microseconds.
 *
 * This function will always wait for a previous call to finish.
 */
void IRAM_ATTR pulse_ckv_us(uint16_t high_time_us, uint16_t low_time_us,
                            bool wait) {
  while (!rmt_tx_done) {
  };
  volatile rmt_item32_t *rmt_mem_ptr =
      &(RMTMEM.chan[row_rmt_config.channel].data32[0]);
  if (high_time_us > 0) {
    rmt_mem_ptr->level0 = 1;
    rmt_mem_ptr->duration0 = high_time_us;
    rmt_mem_ptr->level1 = 0;
    rmt_mem_ptr->duration1 = low_time_us;
  } else {
    rmt_mem_ptr->level0 = 1;
    rmt_mem_ptr->duration0 = low_time_us;
    rmt_mem_ptr->level1 = 0;
    rmt_mem_ptr->duration1 = 0;
  }
  RMTMEM.chan[row_rmt_config.channel].data32[1].val = 0;
  rmt_tx_done = false;
  rmt_tx_start(row_rmt_config.channel, true);
  while (wait && !rmt_tx_done) {
  };
}

void epd_poweron() {
  // POWERON
  config_reg.power_disable = false;
  push_cfg(&config_reg);
  busy_delay(100 * 240);
  config_reg.neg_power_enable = true;
  push_cfg(&config_reg);
  busy_delay(500 * 240);
  config_reg.pos_power_enable = true;
  push_cfg(&config_reg);
  busy_delay(100 * 240);
  config_reg.ep_stv = true;
  push_cfg(&config_reg);
  gpio_set_hi(STH);
  // END POWERON
}

void epd_poweroff() {
  // POWEROFF
  config_reg.pos_power_enable = false;
  push_cfg(&config_reg);
  busy_delay(10 * 240);
  config_reg.neg_power_enable = false;
  push_cfg(&config_reg);
  busy_delay(100 * 240);
  config_reg.power_disable = true;
  push_cfg(&config_reg);
  // END POWEROFF
}

/**
 * This is very timing-sensitive!
 */
void start_frame() {
  // VSCANSTART
  config_reg.ep_mode = true;
  push_cfg(&config_reg);
  // busy_delay(10 * 240);

  /*
gpio_set_lo(STV);
  gpio_set_lo(CKV);
  busy_delay(240);
  gpio_set_hi(CKV);

  gpio_set_hi(STV);
  gpio_set_lo(CKV);
  busy_delay(240);
  gpio_set_hi(CKV);

  */

  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);

  config_reg.ep_stv = false;
  push_cfg(&config_reg);
  busy_delay(240);
  pulse_ckv_us(10, 10, false);
  config_reg.ep_stv = true;
  push_cfg(&config_reg);
  pulse_ckv_us(0, 10, true);

  config_reg.ep_output_enable = true;
  push_cfg(&config_reg); // END VSCANSTART
}

inline void latch_row() {
  config_reg.ep_latch_enable = true;
  push_cfg(&config_reg);

  config_reg.ep_latch_enable = false;
  push_cfg(&config_reg);
}

void skip() {
  latch_row();

  pulse_ckv_us(1, 1, true);
}

void IRAM_ATTR output_row(uint32_t output_time_us, volatile uint8_t *data) {

  while (i2s_is_busy()) {
  };

  latch_row();

  pulse_ckv_us(output_time_us, 10, false);

  if (data != NULL) {
    i2s_start_line_output();
    i2s_switch_buffer();
  }
}

void end_frame() {
  config_reg.ep_output_enable = false;
  push_cfg(&config_reg);
  config_reg.ep_mode = false;
  push_cfg(&config_reg);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
}

void switch_buffer() { i2s_switch_buffer(); }
volatile uint8_t *get_current_buffer() { return i2s_get_current_buffer(); };
