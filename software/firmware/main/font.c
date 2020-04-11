#include "font.h"

#include "EPD.h"
#include <string.h>
#include <stdio.h>
#include "esp_assert.h"
#include <zlib.h>
#include <math.h>
#include "esp_timer.h"

typedef struct {
  uint8_t mask;    /* char data will be bitwise AND with this */
  uint8_t lead;    /* start bytes of current char in utf-8 encoded character */
  uint32_t beg;    /* beginning of codepoint range */
  uint32_t end;    /* end of codepoint range */
  int bits_stored; /* the number of bits from the codepoint that fits in char */
} utf_t;

/*
 * UTF-8 decode inspired from rosetta code
 * https://rosettacode.org/wiki/UTF-8_encode_and_decode#C
 */
utf_t *utf[] = {
    /*             mask        lead        beg      end       bits */
    [0] = &(utf_t){0b00111111, 0b10000000, 0, 0, 6},
    [1] = &(utf_t){0b01111111, 0b00000000, 0000, 0177, 7},
    [2] = &(utf_t){0b00011111, 0b11000000, 0200, 03777, 5},
    [3] = &(utf_t){0b00001111, 0b11100000, 04000, 0177777, 4},
    [4] = &(utf_t){0b00000111, 0b11110000, 0200000, 04177777, 3},
    &(utf_t){0},
};

int utf8_len(const uint8_t ch) {
  int len = 0;
  for (utf_t **u = utf; *u; ++u) {
    if ((ch & ~(*u)->mask) == (*u)->lead) {
      break;
    }
    ++len;
  }
  if (len > 4) { /* Malformed leading byte */
    assert("invalid unicode.");
  }
  return len;
}

uint32_t next_cp(uint8_t **string) {
  if (**string == 0) {
    return 0;
  }
  int bytes = utf8_len(**string);
  uint8_t *chr = *string;
  *string += bytes;
  int shift = utf[0]->bits_stored * (bytes - 1);
  uint32_t codep = (*chr++ & utf[bytes]->mask) << shift;

  for (int i = 1; i < bytes; ++i, ++chr) {
    shift -= utf[0]->bits_stored;
    codep |= ((uint8_t)*chr & utf[0]->mask) << shift;
  }

  return codep;
}

void get_glyph(GFXfont *font, uint32_t code_point, GFXglyph **glyph) {
  UnicodeInterval *intervals = font->intervals;
  *glyph = NULL;
  for (int i = 0; i < font->interval_count; i++) {
    UnicodeInterval *interval = &intervals[i];
    if (code_point >= interval->first && code_point <= interval->last) {
      *glyph = &font->glyph[interval->offset + (code_point - interval->first)];
      return;
    }
    if (code_point < interval->first) {
      return;
    }
  }
  return;
}

/*!
   @brief   Draw a single character to a pre-allocated buffer
*/
void drawChar(GFXfont *font, uint8_t *buffer, int *cursor_x, uint16_t buf_width,
              uint16_t buf_height, int16_t baseline_height, uint32_t cp) {

  GFXglyph *glyph;
  get_glyph(font, cp, &glyph);

  // TODO: Draw Tofu character
  if (!glyph) {
    return;
  }

  uint32_t offset = glyph->data_offset;
  uint8_t width = glyph->width, height = glyph->height;
  int left = glyph->left;

  unsigned long bitmap_size = width * height;
  uint8_t *bitmap = (uint8_t *)malloc(bitmap_size);
  uncompress(bitmap, &bitmap_size, &font->bitmap[offset],
             glyph->compressed_size);
  for (uint32_t i = 0; i < bitmap_size; i++) {
    int xx = *cursor_x + left + i % width;
    int yy = buf_height - (glyph->top + baseline_height - i / width);
    buffer[yy * buf_width + xx] = bitmap[i];
  }
  free(bitmap);
  *cursor_x += glyph->advance_x;
}

/*!
 * @brief Calculate the bounds of a character when drawn at (x, y), move the
 * cursor (*x) forward, adjust the given bounds.
 */
void getCharBounds(GFXfont *font, uint32_t cp, int *x, int *y, int *minx,
                   int *miny, int *maxx, int *maxy) {
  GFXglyph *glyph;
  get_glyph(font, cp, &glyph);

  // TODO: Draw Tofu character
  if (!glyph) {
    return;
  }

  int x1 = *x + glyph->left, y1 = *y + (glyph->top - glyph->height),
      x2 = x1 + glyph->width, y2 = y1 + glyph->height;
  if (x1 < *minx)
    *minx = x1;
  if (y1 < *miny)
    *miny = y1;
  if (x2 > *maxx)
    *maxx = x2;
  if (y2 > *maxy)
    *maxy = y2;
  *x += glyph->advance_x;
}

int min(int x, int y) { return x < y ? x : y; }

void getTextBounds(GFXfont *font, unsigned char *string, int x, int y, int *x1,
                   int *y1, int *w, int *h) {
  int xx = x, yy = y, minx = 100000, miny = 100000, maxx = -1, maxy = -1;
  uint32_t c;
  while ((c = next_cp(&string))) {
    getCharBounds(font, c, &xx, &yy, &minx, &miny, &maxx, &maxy);
  }
  *x1 = min(x, minx);
  *w = maxx - *x1;
  *y1 = miny;
  *h = maxy - miny;
}

void writeln(GFXfont *font, unsigned char *string, int *cursor_x,
             int *cursor_y) {

  int x1 = 0, y1 = 0, w = 0, h = 0;
  getTextBounds(font, string, *cursor_x, *cursor_y, &x1, &y1, &w, &h);
  uint8_t *buffer = (uint8_t *)malloc(w * h);
  memset(buffer, 255, w * h);
  uint32_t c;
  int baseline_height = *cursor_y - y1;

  Rect_t area = {
      .x = x1, .y = *cursor_y - h + baseline_height, .width = w, .height = h};

  int working_curor = 0;
  while ((c = next_cp(&string))) {
    drawChar(font, buffer, &working_curor, w, h, (*cursor_y - y1), c);
  }
  volatile uint32_t t = esp_timer_get_time();
  epd_draw_picture(area, buffer, BIT_DEPTH_4);
  volatile uint32_t t2 = esp_timer_get_time();
  printf("drawing took %d us.\n", t2 - t);
  free(buffer);
}
