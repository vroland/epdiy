#pragma once

#include "lut.h"
#include "freertos/FreeRTOS.h"

void epd_lcd_init();
void s3_set_frame_prepare_cb(void (*cb)(void));
void s3_delete_frame_prepare_cb();
void s3_set_line_source(bool(*line_source)(uint8_t*));
