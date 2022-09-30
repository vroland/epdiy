#pragma once

#include "lut.h"
#include "freertos/FreeRTOS.h"

void epd_lcd_init();
void supply_display(OutputParams* params);
void output_singlecolor_frame(EpdRect area, short time, uint8_t color);
void s3_lcd_enable_data_out();
void s3_lcd_disable_data_out(uint8_t color);
void s3_set_frame_prepare_cb(void (*cb)(void));
void s3_delete_frame_prepare_cb();
void s3_set_display_queue(QueueHandle_t line_queue);
