/*
 * A more high-level library for drawing to an EPD.
 */
#pragma once
#include "esp_attr.h"
#include <stdint.h>

#define EPD_WIDTH 1200
#define EPD_HEIGHT 825

typedef struct {
  uint16_t x;
  uint16_t y;
  uint16_t width;
  uint16_t height;
} Rect_t;

/* initialize the ePaper display */
void epd_init();

/* enable display power supply. */
void epd_poweron();

/* disable display power supply. */
void epd_poweroff();

/* clear the whole screen. */
void epd_clear();

/* Clear an area by flashing it. */
void epd_clear_area(Rect_t area);

/*
 * Draw a picture to a given area. The picture must be given as
 * sequence of 4-bit brightness values, packed as two pixels per byte.
 * A byte cannot wrap over multiple rows, but if the image size is given as
 * uneven, the last half byte is ignored.
 * The given area must be white before drawing.
 */
void IRAM_ATTR epd_draw_grayscale_image(Rect_t area, uint8_t *data);

/*
 * Returns a rectancle representing the whole screen area.
 */
Rect_t epd_full_screen();
