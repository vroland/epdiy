#include "render.h"
#include "display_ops.h"
#include "include/epd_driver.h"

void i2s_write_row(uint32_t output_time_dus);
void i2s_skip_row(uint8_t pipeline_finish_time);

void epd_push_pixels_i2s(EpdRect area, short time, int color);
