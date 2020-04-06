#include "ed097oc4.h"

#include <string.h>

#include "driver/periph_ctrl.h"
#include "driver/rmt.h"
#include "esp_heap_caps.h"
#include "esp_intr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/lldesc.h"
#include "soc/i2s_reg.h"
#include "soc/i2s_struct.h"
#include "soc/rtc.h"

// compensate for big-endian ordering in output data.
int I2S_GPIO_BUS[] = {D6, D7, D4, D5, D2, D3, D0, D1,
                      -1, -1, -1, -1, -1, -1, -1, -1};

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

typedef struct {
  volatile lldesc_t *dma_desc_a;
  volatile lldesc_t *dma_desc_b;
} i2s_parallel_state_t;

epd_config_register_t config_reg;

static i2s_parallel_state_t i2s_state;

uint8_t buf_a[EPD_LINE_BYTES];
uint8_t buf_b[EPD_LINE_BYTES];

int current_buffer = 0;

volatile bool output_done = true;
static intr_handle_t gI2S_intr_handle = NULL;
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

static void fill_dma_desc(volatile lldesc_t *dmadesc, uint8_t *buf) {
  dmadesc->size = EPD_LINE_BYTES;
  dmadesc->length = EPD_LINE_BYTES;
  dmadesc->buf = buf;
  dmadesc->eof = 1;
  dmadesc->sosf = 1;
  dmadesc->owner = 1;
  dmadesc->qe.stqe_next = 0;
  dmadesc->offset = 0;
}

static void gpio_setup_out(int gpio, int sig, bool invert) {
  if (gpio == -1)
    return;
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[gpio], PIN_FUNC_GPIO);
  gpio_set_direction(gpio, GPIO_MODE_DEF_OUTPUT);
  gpio_matrix_out(gpio, sig, invert, false);
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

// ISR handler. Call callback to refill buffer that was just finished.
static void IRAM_ATTR i2s_int_hdl(void *arg) {
  i2s_dev_t *dev = &I2S1;
  if (dev->int_st.out_done) {
    // dev->int_ena.tx_rempty=1;
    // Clear the interrupt. Otherwise, the whole device would hang.
    fast_gpio_set_hi(STH);
    output_done = true;
  }
  dev->int_clr.val = dev->int_raw.val;
}

// -- Custom interrupt handler
// Signal when the RMT is done.
static void IRAM_ATTR rmt_interrupt_handler(void *arg) {
  // -- The basic structure of this code is borrowed from the
  //    interrupt handler in esp-idf/components/driver/rmt.c
  rmt_tx_done = true;
  RMT.int_clr.val = RMT.int_st.val;
}

volatile uint8_t *get_current_buffer() {
  return current_buffer ? i2s_state.dma_desc_a->buf : i2s_state.dma_desc_b->buf;
}

uint32_t dma_desc_addr() {
  return (uint32_t)(current_buffer ? i2s_state.dma_desc_b
                                   : i2s_state.dma_desc_a);
}

void switch_buffer() { current_buffer = !current_buffer; }

void i2s_setup(i2s_dev_t *dev) {

  // Use I2S1 with no signal offset (for some reason the offset seems to be
  // needed in 16-bit mode, but not in 8 bit mode.
  int signal_base = I2S1O_DATA_OUT0_IDX;

  // Setup and route GPIOS
  for (int x = 0; x < 8; x++) {
    gpio_setup_out(I2S_GPIO_BUS[x], signal_base + x, false);
  }
  // Invert word select signal
  gpio_setup_out(CKH, I2S1O_WS_OUT_IDX, true);

  periph_module_enable(PERIPH_I2S1_MODULE);

  // Initialize device
  dev->conf.tx_reset = 1;
  dev->conf.tx_reset = 0;

  // Reset DMA
  dev->lc_conf.in_rst = 1;
  dev->lc_conf.in_rst = 0;
  dev->lc_conf.out_rst = 1;
  dev->lc_conf.out_rst = 0;

  //////////// Setup I2S config. See section 12 of Technical Reference Manual
  /////////////
  // Enable LCD mode
  dev->conf2.val = 0;
  dev->conf2.lcd_en = 1;

  // Enable FRAME1-Mode (See technical reference manual)
  dev->conf2.lcd_tx_wrx2_en = 1;
  dev->conf2.lcd_tx_sdx2_en = 0;

  // Set to 8 bit parallel output
  dev->sample_rate_conf.val = 0;
  dev->sample_rate_conf.tx_bits_mod = 8;

  // Half speed of bit clock in LCD mode.
  dev->sample_rate_conf.tx_bck_div_num = 2;

  // Initialize Audio Clock (APLL) for 48 Mhz.
  rtc_clk_apll_enable(1, 0, 0, 8, 3);

  // Enable undivided Audio Clock
  dev->clkm_conf.val = 0;
  dev->clkm_conf.clka_en = 1;
  dev->clkm_conf.clkm_div_a = 1;
  dev->clkm_conf.clkm_div_b = 0;

  // The smallest stable divider for the internal PLL is 6 (13.33 MHz),
  // While with the APLL, the display is stable up to 48 MHz!
  // (Which is later halved by bck_div, so we use 24 MHz)
  dev->clkm_conf.clkm_div_num = 1;

  // Set up FIFO
  dev->fifo_conf.val = 0;
  dev->fifo_conf.tx_fifo_mod_force_en = 1;
  dev->fifo_conf.tx_fifo_mod = 1;
  dev->fifo_conf.tx_data_num = 32;
  dev->fifo_conf.dscr_en = 1;

  // Stop after transmission complete
  dev->conf1.val = 0;
  dev->conf1.tx_stop_en = 1;
  dev->conf1.tx_pcm_bypass = 1;

  // Configure TX channel
  dev->conf_chan.val = 0;
  dev->conf_chan.tx_chan_mod = 1;
  dev->conf.tx_right_first = 1;

  dev->timing.val = 0;

  // Allocate DMA descriptors
  i2s_state.dma_desc_a = heap_caps_malloc(sizeof(lldesc_t), MALLOC_CAP_DMA);
  i2s_state.dma_desc_b = heap_caps_malloc(sizeof(lldesc_t), MALLOC_CAP_DMA);

  // and fill them
  fill_dma_desc(i2s_state.dma_desc_a, buf_a);
  fill_dma_desc(i2s_state.dma_desc_b, buf_b);

  // enable "done" interrupt
  SET_PERI_REG_BITS(I2S_INT_ENA_REG(1), I2S_OUT_DONE_INT_ENA_V, 1,
                    I2S_OUT_DONE_INT_ENA_S);
  // register interrupt
  esp_intr_alloc(ETS_I2S1_INTR_SOURCE, 0, i2s_int_hdl, 0, &gI2S_intr_handle);

  // Reset FIFO/DMA
  dev->lc_conf.in_rst = 1;
  dev->lc_conf.out_rst = 1;
  dev->lc_conf.ahbm_rst = 1;
  dev->lc_conf.ahbm_fifo_rst = 1;
  dev->lc_conf.in_rst = 0;
  dev->lc_conf.out_rst = 0;
  dev->lc_conf.ahbm_rst = 0;
  dev->lc_conf.ahbm_fifo_rst = 0;
  dev->conf.tx_reset = 1;
  dev->conf.tx_fifo_reset = 1;
  dev->conf.rx_fifo_reset = 1;
  dev->conf.tx_reset = 0;
  dev->conf.tx_fifo_reset = 0;
  dev->conf.rx_fifo_reset = 0;

  // Start dma on front buffer
  dev->lc_conf.val =
      I2S_OUT_DATA_BURST_EN | I2S_OUTDSCR_BURST_EN | I2S_OUT_DATA_BURST_EN;
  dev->out_link.addr = ((uint32_t)(i2s_state.dma_desc_a));
  dev->out_link.start = 1;

  dev->int_clr.val = dev->int_raw.val;

  dev->int_ena.val = 0;
  dev->int_ena.out_done = 1;

  dev->conf.tx_start = 0;
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

  gpio_set_direction(STH, GPIO_MODE_OUTPUT);
  gpio_set_lo(STH);
  gpio_set_direction(CKV, GPIO_MODE_OUTPUT);
  gpio_set_lo(CKV);

  /* Output lines are set up in i2s_setup */
  // Setup I2S
  i2s_setup(&I2S1);

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

/*
 * Start shifting out the current buffer via I2S.
 */
void IRAM_ATTR start_line_output() {
  i2s_dev_t *dev = &I2S1;
  dev->conf.tx_start = 0;
  dev->conf.tx_reset = 1;
  dev->conf.tx_fifo_reset = 1;
  dev->conf.rx_fifo_reset = 1;
  dev->conf.tx_reset = 0;
  dev->conf.tx_fifo_reset = 0;
  dev->conf.rx_fifo_reset = 0;
  dev->out_link.addr = dma_desc_addr();
  dev->out_link.start = 1;
  dev->conf.tx_start = 1;
}

void skip() {
  latch_row();

  pulse_ckv_us(1, 1, true);
}

void IRAM_ATTR output_row(uint32_t output_time_us, volatile uint8_t *data) {

  // wait for dma to be done with the line
  while (!output_done) {
  };
  // now wait until the fifo buffer is empty as well.
  while (!I2S1.state.tx_idle) {
  };

  latch_row();

  pulse_ckv_us(output_time_us, 10, false);

  if (data != NULL) {
    output_done = false;
    fast_gpio_set_lo(STH);
    start_line_output();
    switch_buffer();

    // sth is pulled up through peripheral interrupt
  }

  // wait_line(output_time_us);
}

void end_frame() {
  config_reg.ep_output_enable = false;
  push_cfg(&config_reg);
  config_reg.ep_mode = false;
  push_cfg(&config_reg);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
}
