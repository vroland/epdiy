#include "epd_display.h"

#include "epd_driver.h"
#include <stddef.h>

EpdDisplayDefinition epd_display = {0};

void epd_set_display(uint16_t width, uint16_t height) {
  epd_display.width = width;
  epd_display.height = height;
}
