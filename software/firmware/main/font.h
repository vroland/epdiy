// Font structures adapted from Adafruit_GFX

#ifndef _FONT_H_
#define _FONT_H_

#include <stdint.h>

/// Font data stored PER GLYPH
typedef struct {
  uint8_t width;            ///< Bitmap dimensions in pixels
  uint8_t height;           ///< Bitmap dimensions in pixels
  uint8_t advance_x;        ///< Distance to advance cursor (x axis)
  int16_t left;             ///< X dist from cursor pos to UL corner
  int16_t top;              ///< Y dist from cursor pos to UL corner
  uint16_t compressed_size; ///< Size of the zlib-compressed font data.
  uint32_t data_offset;     ///< Pointer into GFXfont->bitmap
} GFXglyph;

/// Glyph interval structure
typedef struct {
  uint32_t first;  ///< The first unicode code point of the interval
  uint32_t last;   ///< The last unicode code point of the interval
  uint32_t offset; ///< Index of the first code point into the glyph array
} UnicodeInterval;

/// Data stored for FONT AS A WHOLE
typedef struct {
  uint8_t *bitmap;            ///< Glyph bitmaps, concatenated
  GFXglyph *glyph;            ///< Glyph array
  UnicodeInterval *intervals; ///< Valid unicode intervals for this font
  uint32_t interval_count;    ///< Number of unicode intervals.
  uint8_t advance_y;          ///< Newline distance (y axis)
} GFXfont;

/*!
 * Get the text bounds for string, when drawn at (x, y).
 */
void get_text_bounds(GFXfont *font, char *string, int x, int y, int *x1,
                     int *y1, int *w, int *h);

/*!
 * Write a line of text to the EPD.
 */
void writeln(GFXfont *font, char *string, int *cursor_x, int *cursor_y,
             uint8_t *framebuffer);

/**
 * Write a (multi-line) string to the EPD.
 */
void write_string(GFXfont *font, char *string, int *cursor_x, int *cursor_y,
             	  uint8_t *framebuffer);
#endif // _FONT_H_
