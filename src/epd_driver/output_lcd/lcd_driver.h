#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <driver/gpio.h>


/**
 * LCD bus configuration parameters.
 */
typedef struct {
  // GPIO numbers of the parallel bus pins.
  gpio_num_t data_0;
  gpio_num_t data_1;
  gpio_num_t data_2;
  gpio_num_t data_3;
  gpio_num_t data_4;
  gpio_num_t data_5;
  gpio_num_t data_6;
  gpio_num_t data_7;


  gpio_num_t data_8;
  gpio_num_t data_9;
  gpio_num_t data_10;
  gpio_num_t data_11;
  gpio_num_t data_12;
  gpio_num_t data_13;
  gpio_num_t data_14;
  gpio_num_t data_15;

  // horizontal clock pin.
  gpio_num_t clock;
  // vertical clock pin
  gpio_num_t ckv;

  // horizontal "Start Pulse", enabling data input on the line shift register
  gpio_num_t start_pulse;
  // latch enable
  gpio_num_t leh;
  // vertical start pulse, resetting the vertical line shift register.
  gpio_num_t stv;
} lcd_bus_config_t;

/// Configuration structure for the LCD-based Epd driver.
typedef struct {
    // high time for CKV in 1/10us.
    size_t pixel_clock; // = 12000000
    int ckv_high_time; // = 70
    int line_front_porch; // = 4
    int le_high_time; // = 4
    int bus_width; // = 16
    lcd_bus_config_t bus;
} LcdEpdConfig_t;

typedef bool(*line_cb_func_t)(void*, uint8_t*);
typedef void(*frame_done_func_t)(void*);

void epd_lcd_init(const LcdEpdConfig_t* config);
void epd_lcd_frame_done_cb(frame_done_func_t, void* payload);
void epd_lcd_line_source_cb(line_cb_func_t, void* payload);
void epd_lcd_start_frame();
