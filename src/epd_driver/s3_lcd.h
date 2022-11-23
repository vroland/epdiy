#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/// Configuration structure for the LCD-based Epd driver.
typedef struct {
    // high time for CKV in 1/10us.
    size_t pixel_clock; // = 12000000
    int ckv_high_time; // = 70
    int line_front_porch; // = 4
    int le_high_time; // = 4
    int bus_width; // = 16
} LcdEpdConfig_t;

void epd_lcd_init(const LcdEpdConfig_t* config);
void epd_lcd_frame_done_cb(void (*cb)(void));
void epd_lcd_line_source_cb(bool(*line_source)(uint8_t*));
void epd_lcd_start_frame();
