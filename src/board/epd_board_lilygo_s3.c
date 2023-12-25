#include "epd_board.h"
#include "epd_board_lilygo_s3.h"
#include "epdiy.h"
#include <stdint.h>

#include "esp_log.h"
#include "../output_lcd/lcd_driver_lilygo.h"
#include "../output_common/render_method.h"
//#include "../output_i2s/rmt_pulse.h"

#include <sdkconfig.h>
#include <driver/gpio.h>
#include <driver/i2c.h>
#include "esp_system.h"  // for ESP_IDF_VERSION_VAL
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"
#endif

// Make this compile von the ESP32 without ifdefing the whole file
#ifndef CONFIG_IDF_TARGET_ESP32S3
#define GPIO_NUM_40 -1
#define GPIO_NUM_41 -1
#define GPIO_NUM_42 -1
#define GPIO_NUM_43 -1
#define GPIO_NUM_44 -1
#define GPIO_NUM_45 -1
#define GPIO_NUM_46 -1
#define GPIO_NUM_47 -1
#define GPIO_NUM_48 -1
#endif


#define CFG_SCL             GPIO_NUM_40
#define CFG_SDA             GPIO_NUM_39
#define CFG_INTR            GPIO_NUM_38
#define EPDIY_I2C_PORT      I2C_NUM_0

/* Config Register Control */
#define CFG_DATA GPIO_NUM_13
#define CFG_CLK GPIO_NUM_12
#define CFG_STR GPIO_NUM_0

/* Data Lines */
#define D7 GPIO_NUM_7
#define D6 GPIO_NUM_6
#define D5 GPIO_NUM_5
#define D4 GPIO_NUM_4
#define D3 GPIO_NUM_3
#define D2 GPIO_NUM_2
#define D1 GPIO_NUM_1
#define D0 GPIO_NUM_8

/* Control Lines */
#define CKV GPIO_NUM_38
#define STH GPIO_NUM_40

/* Edges */
#define CKH GPIO_NUM_41

/* Control Lines */
// Are on the IO expander, redirect this GPIO triggered by LCD
#define LEH_INT GPIO_NUM_15

static lcd_bus_config_t lcd_config = {
  .data_0 = D0,
  .data_1 = D1,
  .data_2 = D2,
  .data_3 = D3,
  .data_4 = D4,
  .data_5 = D5,
  .data_6 = D6,
  .data_7 = D7,
  // Signals
  .clock = CKH,
  .ckv = CKV,
  .start_pulse = STH,
  .leh = LEH_INT
};

inline static void fast_gpio_set_lo(gpio_num_t gpio_num) {
  GPIO.out_w1tc = (1 << gpio_num);
}

inline static void fast_gpio_set_hi(gpio_num_t gpio_num) {
  GPIO.out_w1ts = (1 << gpio_num);
}

static void IRAM_ATTR push_cfg_bit(bool bit) {
    fast_gpio_set_lo(CFG_CLK);
    if (bit)
    {
        fast_gpio_set_hi(CFG_DATA);
    }
    else
    {
        fast_gpio_set_lo(CFG_DATA);
    }
    fast_gpio_set_hi(CFG_CLK);
}

static void IRAM_ATTR push_cfg(epd_config_register_t *cfg)
{
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

static epd_config_register_t config_reg;

static QueueHandle_t gpio_evt_queue = NULL;

static void IRAM_ATTR gpio_leh_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    config_reg.ep_latch_enable = gpio_get_level(LEH_INT);
    push_cfg(&config_reg);
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}
static void gpio_task_example(void* arg)
{
    uint32_t io_num;
    for (;;) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            printf("GPIO[%"PRIu32"] intr, val: %d\n", io_num, gpio_get_level(io_num));
        }
    }
}

static void epd_board_init(uint32_t epd_row_width) {
  // Start I2C
    gpio_hold_dis(CKH); // free CKH after wakeup

    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = CFG_SDA;
    conf.scl_io_num = CFG_SCL;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 100000;
    conf.clk_flags = 0;
    ESP_ERROR_CHECK(i2c_param_config(EPDIY_I2C_PORT, &conf));

    ESP_ERROR_CHECK(i2c_driver_install(EPDIY_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));

    //create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    //start gpio task
    xTaskCreate(gpio_task_example, "gpio_task_example", 2048, NULL, 10, NULL);

  gpio_reset_pin(CFG_DATA);
  gpio_reset_pin(CFG_CLK);
  gpio_reset_pin(CFG_STR);
  gpio_set_direction(CFG_DATA, GPIO_MODE_OUTPUT);
  gpio_set_direction(CFG_CLK, GPIO_MODE_OUTPUT);
  gpio_set_direction(CFG_STR, GPIO_MODE_OUTPUT);

  // Make LEH to trigger an interrupt. Such a bad and silly idea
  /* gpio_set_intr_type(LEH_INT, GPIO_INTR_ANYEDGE);
  gpio_install_isr_service(0);
  gpio_isr_handler_add(LEH_INT, gpio_leh_handler, (void*) LEH_INT); */

  /* Power Control Output/Off */
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[CFG_DATA], PIN_FUNC_GPIO);
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[CFG_CLK], PIN_FUNC_GPIO);
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[CFG_STR], PIN_FUNC_GPIO);
 
  
  fast_gpio_set_lo(CFG_STR);

  config_reg.ep_latch_enable = false;
  config_reg.power_disable = true;
  config_reg.pos_power_enable = false;
  config_reg.neg_power_enable = false;
  config_reg.ep_stv = true;
  config_reg.ep_scan_direction = true;
  config_reg.ep_mode = false;
  config_reg.ep_output_enable = false;
  push_cfg(&config_reg);

  // start LCD module
  const EpdDisplay_t* display = epd_get_display();

    LcdEpdConfig_t config = {
        .pixel_clock = display->bus_speed * 1000 * 1000,
        .ckv_high_time = 60,
        .line_front_porch = 4,
        .le_high_time = 4,
        .bus_width = display->bus_width,
        .bus = lcd_config,
    };
    epd_lcd_init(&config, display->width, display->height);

  // CKV is now handled by LCD module
  //rmt_pulse_init(CKV);
}

static void epd_board_set_ctrl(epd_ctrl_state_t *state, const epd_ctrl_state_t * const mask) {
  if (state->ep_sth) {
    fast_gpio_set_hi(STH);
  } else {
    fast_gpio_set_lo(STH);
  }

  if (mask->ep_output_enable || mask->ep_mode || mask->ep_stv || mask->ep_latch_enable) {
    fast_gpio_set_lo(CFG_STR);

    // push config bits in reverse order
    push_cfg_bit(state->ep_output_enable);
    push_cfg_bit(state->ep_mode);
    push_cfg_bit(config_reg.ep_scan_direction);
    push_cfg_bit(state->ep_stv);

    push_cfg_bit(config_reg.neg_power_enable);
    push_cfg_bit(config_reg.pos_power_enable);
    push_cfg_bit(config_reg.power_disable);
    push_cfg_bit(state->ep_latch_enable);

    fast_gpio_set_hi(CFG_STR);
  }
}

static void epd_board_poweron(epd_ctrl_state_t *state) {
  //i2s_gpio_attach(&i2s_config);
    config_reg.ep_scan_direction = true;
    config_reg.power_disable = false;
    push_cfg(&config_reg);
    epd_busy_delay(100 * 240);
    config_reg.neg_power_enable = true;
    push_cfg(&config_reg);
    epd_busy_delay(500 * 240);
    config_reg.pos_power_enable = true;
    push_cfg(&config_reg);
    epd_busy_delay(100 * 240);
    config_reg.ep_stv = true;
    push_cfg(&config_reg);
    fast_gpio_set_hi(STH);
}

static void epd_board_poweroff_common(epd_ctrl_state_t *state) {
  config_reg.pos_power_enable = false;
  push_cfg(&config_reg);
  epd_busy_delay(10 * 240);
  config_reg.neg_power_enable = false;
  push_cfg(&config_reg);
  epd_busy_delay(100 * 240);
  config_reg.power_disable = true;
  push_cfg(&config_reg);

  config_reg.ep_stv = false;
  push_cfg(&config_reg);

}

static void epd_board_poweroff(epd_ctrl_state_t *state) {
  epd_board_poweroff_common(state);
}

static void epd_board_poweroff_touch(epd_ctrl_state_t *state) {
  // This was re-purposed as power enable.
  config_reg.ep_scan_direction = true;
  epd_board_poweroff_common(state);
}


const EpdBoardDefinition epd_board_lilygo_s3_47 = {
  .init = epd_board_init,
  .deinit = NULL,
  .set_ctrl = epd_board_set_ctrl,
  .poweron = epd_board_poweron,
  .poweroff = epd_board_poweroff,
};

