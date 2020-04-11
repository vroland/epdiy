/*
 * A more high-level library for drawing to an EPD.
 */
#pragma once
#include <stdint.h>
#include "esp_attr.h"

typedef struct {
  uint16_t x;
  uint16_t y;
  uint16_t width;
  uint16_t height;
} Rect_t;

typedef enum {
  BIT_DEPTH_4 = 4,
  BIT_DEPTH_2 = 2,
} EPDBitdepth_t;

/* initialize the ePaper display */
void epd_init();

/* enable display power supply. */
void epd_poweron();

/* disable display power supply. */
void epd_poweroff();

/* clear the whole screen. */
void epd_clear();

/* Clear an area by flashing it white -> black -> white */
void epd_clear_area(Rect_t area);

/*
 * Draw a picture to a given area. The picture must be given as
 * sequence of 4-bit brightness values, packed as two pixels per byte.
 * A byte cannot wrap over multiple rows, but if the image size is given as
 * uneven, the last half byte is ignored.
 * The given area must be white before drawing.
 */
void IRAM_ATTR epd_draw_picture(Rect_t area, uint8_t *data, EPDBitdepth_t bpp);

/*
 * Returns a rectancle representing the whole screen area.
 */
Rect_t epd_full_screen();

/* draw a frame with all pixels being set to `byte`,
 * where byte is an EPD-specific encoding of black and white. */
void epd_draw_byte(Rect_t *area, short time, uint8_t byte);


void img_8bit_to_unary_image(uint8_t *dst, uint8_t *src, uint32_t image_width, uint32_t image_height);


void IRAM_ATTR draw_image_unary_coded(Rect_t area, uint8_t* data);
