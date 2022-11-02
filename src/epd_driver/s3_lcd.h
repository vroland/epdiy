#pragma once

#include "lut.h"
#include "freertos/FreeRTOS.h"

esp_err_t epd_lcd_init();
void s3_set_frame_prepare_cb(void (*cb)(void));
void s3_delete_frame_prepare_cb();
void s3_set_line_source(bool(*line_source)(uint8_t*));
void s3_start_transmission();
