#pragma once
#include "Arduino.h"

#define EPD_WIDTH 1200
#define EPD_HEIGHT 825
// number of bytes needed for one line of EPD pixel data.
#define EPD_LINE_BYTES 1200/4

/* Control Lines */
#define NEG_CTRL  GPIO_NUM_33  // Active HIGH
#define POS_CTRL  GPIO_NUM_32  // Active HIGH
#define SMPS_CTRL GPIO_NUM_18  // Active LOW

/* Control Lines */
#define CKV       GPIO_NUM_25
#define STV       GPIO_NUM_27
#define MODE      GPIO_NUM_0
#define STH       GPIO_NUM_26
#define OEH       GPIO_NUM_19

/* Edges */
#define CKH       GPIO_NUM_23
#define LEH       GPIO_NUM_2

/* Data Lines */
#define D7        GPIO_NUM_17
#define D6        GPIO_NUM_16
#define D5        GPIO_NUM_15
#define D4        GPIO_NUM_14
#define D3        GPIO_NUM_13
#define D2        GPIO_NUM_12
#define D1        GPIO_NUM_5
#define D0        GPIO_NUM_4

// Masks the data pins in a gpio bitmap to enable fast writing.
#define DATA_GPIO_MASK 0B00000000000000111111000000110000

void init_gpios();
void epd_poweron();
void epd_poweroff();

/*
 * Start a draw cycle.
 */
void start_frame();

/*
 * Output `data` to the next display row and enable the vertical
 * driver for `output_time_us` microseconds.
 */
void output_row(uint32_t output_time_us, uint8_t* data);

/* skip a row without writing to it. */
void skip();

/* enable the output register */
void enable_output();

/* disable the output register */
void disable_output();

/*
 * End a draw cycle.
 */
void end_frame();
