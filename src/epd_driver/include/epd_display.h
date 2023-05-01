/**
 * @file "epd_display.h"
 * @brief Display-definitions provided by epdiy.
 */

#pragma once

#include <stdint.h>

typedef struct {
  uint16_t width;
  uint16_t height;
} EpdDisplayDefinition;

extern EpdDisplayDefinition epd_display;

void epd_set_display(uint16_t width, uint16_t height);
