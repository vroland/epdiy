#include <stdint.h>
#include "epd_board.h"
#include "epdiy.h"

#include "../output_common/render_method.h"
#include "../output_lcd/lcd_driver.h"
#include "esp_log.h"

#include <driver/gpio.h>
#include <driver/i2c.h>
#include <sdkconfig.h>

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

#define EPD_SPV GPIO_NUM_17
#define EPD_EN  GPIO_NUM_45
#define BST_EN  GPIO_NUM_46
#define EPD_XLE GPIO_NUM_15

/* Control Lines */
#define CKV GPIO_NUM_18
#define STH GPIO_NUM_13

/* Edges */
#define CKH GPIO_NUM_16

/* Data Lines */
#define D7 GPIO_NUM_10
#define D6 GPIO_NUM_8
#define D5 GPIO_NUM_11
#define D4 GPIO_NUM_9
#define D3 GPIO_NUM_12
#define D2 GPIO_NUM_7
#define D1 GPIO_NUM_14
#define D0 GPIO_NUM_6

static lcd_bus_config_t lcd_config = {
    .clock = CKH,
    .ckv = CKV,
    .leh = EPD_XLE,
    .start_pulse = STH,
    .stv = EPD_SPV,
    .data[0] = D0,
    .data[1] = D1,
    .data[2] = D2,
    .data[3] = D3,
    .data[4] = D4,
    .data[5] = D5,
    .data[6] = D6,
    .data[7] = D7,
};

static void epd_board_init(uint32_t epd_row_width) {
    gpio_hold_dis(CKH);  // free CKH after wakeup

    gpio_set_direction(EPD_SPV, GPIO_MODE_OUTPUT);
    gpio_set_direction(EPD_EN, GPIO_MODE_OUTPUT);
    gpio_set_direction(BST_EN, GPIO_MODE_OUTPUT);
    gpio_set_direction(EPD_XLE, GPIO_MODE_OUTPUT);

    gpio_set_level(EPD_XLE, 0);
    gpio_set_level(EPD_SPV, 1);
    gpio_set_level(EPD_EN, 0);
    gpio_set_level(BST_EN, 0);

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
}

static void epd_board_deinit() {
    epd_lcd_deinit();

    gpio_set_level(EPD_XLE, 0);
    gpio_set_level(EPD_SPV, 0);
    gpio_set_level(EPD_EN, 0);
    gpio_set_level(BST_EN, 0);
}

static void epd_board_set_ctrl(epd_ctrl_state_t* state, const epd_ctrl_state_t* const mask) {
  if (state->ep_sth) {
    gpio_set_level(STH, 1);
  } else {
    gpio_set_level(STH, 0);
  }

  if (state->ep_stv) {
    gpio_set_level(EPD_SPV, 1);
  } else {
    gpio_set_level(EPD_SPV, 0);
  }

  if (state->ep_latch_enable) {
    gpio_set_level(EPD_XLE, 1);
  } else {
    gpio_set_level(EPD_XLE, 0);
  }
}

static void epd_board_poweron(epd_ctrl_state_t* state) {
    gpio_set_level(EPD_EN, 1);
    vTaskDelay(1 / portTICK_PERIOD_MS);
    gpio_set_level(BST_EN, 1);
    vTaskDelay(1 / portTICK_PERIOD_MS);
    gpio_set_level(EPD_SPV, 1);
    gpio_set_level(EPD_SPV, 1);
}

static void epd_board_poweroff(epd_ctrl_state_t* state) {
  // gpio_set_level(BST_EN,0);
  // vTaskDelay(1 / portTICK_PERIOD_MS);
  // gpio_set_level(EPD_EN,0);
  // vTaskDelay(1 / portTICK_PERIOD_MS);
  gpio_set_level(EPD_SPV,0);
}

static float epd_board_ambient_temperature() {
    return 20;
}

const EpdBoardDefinition epd_board_m5papers3 = {
    .init = epd_board_init,
    .deinit = epd_board_deinit,
    .set_ctrl = epd_board_set_ctrl,
    .poweron = epd_board_poweron,
    .poweroff = epd_board_poweroff,

    .get_temperature = epd_board_ambient_temperature,
    .set_vcom = NULL,

    // unimplemented for now, but shares v6 implementation
    .gpio_set_direction = NULL,
    .gpio_read = NULL,
    .gpio_write = NULL,
};