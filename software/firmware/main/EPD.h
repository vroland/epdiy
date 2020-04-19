/**
 * A high-level library for drawing to an EPD.
 */
#pragma once
#include "esp_attr.h"
#include <stdint.h>

/// Width of the display area in pixels.
#define EPD_WIDTH 1200
/// Height of the display area in pixels.
#define EPD_HEIGHT 825

/// An area on the display.
typedef struct {
  /// Horizontal position.
  int x;
  /// Vertical position.
  int y;
  /// Area / image width, must be positive.
  int width;
  /// Area / image height, must be positive.
  int height;
} Rect_t;

/** Initialize the ePaper display */
void epd_init();

/** Enable display power supply. */
void epd_poweron();

/** Disable display power supply. */
void epd_poweroff();

/** Clear the whole screen by flashing it. */
void epd_clear();

/**
 * Clear an area by flashing it.
 *
 * @param area: The area to clear.
 */
void epd_clear_area(Rect_t area);

/**
 * Draw a picture to a given area. The image area is not cleared and assumed
 * to be white before drawing.
 *
 * @param area: The display area to draw to. `width` and `height` of the area
 *   must correspond to the image dimensions in pixels.
 * @param data: The image data, as a buffer of 4 bit wide brightness values.
 *   Pixel data is packed (two pixels per byte). A byte cannot wrap over multiple
 *   rows, images of uneven width must add a padding nibble per line.
 */
void IRAM_ATTR epd_draw_grayscale_image(Rect_t area, uint8_t *data);

/**
 * @returns Rectancle representing the whole screen area.
 */
Rect_t epd_full_screen();

/**
 * Draw a picture to a given framebuffer.
 *
 * @param image_area: The area to copy to. `width` and `height` of the area
 *   must correspond to the image dimensions in pixels.
 * @param image_data: The image data, as a buffer of 4 bit wide brightness values.
 *   Pixel data is packed (two pixels per byte). A byte cannot wrap over multiple
 *   rows, images of uneven width must add a padding nibble per line.
 * @param framebuffer: The framebuffer object,
 *   which must be `EPD_WIDTH / 2 * EPD_HEIGHT` large.
 */
void epd_copy_to_framebuffer(Rect_t image_area, uint8_t *image_data,
                             uint8_t *framebuffer);

/**
 * Draw a horizontal line to a given framebuffer.
 *
 * @param x: Horizontal start position in pixels.
 * @param y: Vertical start position in pixels.
 * @param length: Length of the line in pixels.
 * @param color: The gray value of the line (0-255);
 * @param framebuffer: The framebuffer to draw to,
 *  which must be `EPD_WIDTH / 2 * EPD_HEIGHT` bytes large.
 */
void epd_draw_hline(int x, int y, int length, uint8_t color,
                    uint8_t *framebuffer);

/**
 * Draw a horizontal line to a given framebuffer.
 *
 * @param x: Horizontal start position in pixels.
 * @param y: Vertical start position in pixels.
 * @param length: Length of the line in pixels.
 * @param color: The gray value of the line (0-255);
 * @param framebuffer: The framebuffer to draw to,
 *  which must be `EPD_WIDTH / 2 * EPD_HEIGHT` bytes large.
 */
void epd_draw_vline(int x, int y, int length, uint8_t color,
                    uint8_t *framebuffer);
