#include "epd_driver.h"
#include "esp_assert.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp32/rom/miniz.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

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
static utf_t *utf[] = {
    /*             mask        lead        beg      end       bits */
    [0] = &(utf_t){0b00111111, 0b10000000, 0, 0, 6},
    [1] = &(utf_t){0b01111111, 0b00000000, 0000, 0177, 7},
    [2] = &(utf_t){0b00011111, 0b11000000, 0200, 03777, 5},
    [3] = &(utf_t){0b00001111, 0b11100000, 04000, 0177777, 4},
    [4] = &(utf_t){0b00000111, 0b11110000, 0200000, 04177777, 3},
    &(utf_t){0},
};

/**
 * static decompressor object for compressed fonts.
 */
static tinfl_decompressor decomp;

static inline int min(int x, int y) { return x < y ? x : y; }
static inline int max(int x, int y) { return x > y ? x : y; }

static int utf8_len(const uint8_t ch) {
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

static uint32_t next_cp(const uint8_t **string) {
  if (**string == 0) {
    return 0;
  }
  int bytes = utf8_len(**string);
  const uint8_t *chr = *string;
  *string += bytes;
  int shift = utf[0]->bits_stored * (bytes - 1);
  uint32_t codep = (*chr++ & utf[bytes]->mask) << shift;

  for (int i = 1; i < bytes; ++i, ++chr) {
    shift -= utf[0]->bits_stored;
    codep |= ((const uint8_t)*chr & utf[0]->mask) << shift;
  }

  return codep;
}

static FontProperties font_properties_default() {
  FontProperties props = {
      .fg_color = 0, .bg_color = 15, .fallback_glyph = 0, .flags = 0};
  return props;
}

void get_glyph(const GFXfont *font, uint32_t code_point,
               const GFXglyph **glyph) {
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

static int uncompress(uint8_t *dest, uint32_t uncompressed_size, uint8_t *source, uint32_t source_size) {
    if (uncompressed_size == 0 || dest == NULL || source_size == 0 || source == NULL) {
        return -1;
    }
    tinfl_init(&decomp);

    // we know everything will fit into the buffer.
    tinfl_status decomp_status = tinfl_decompress(&decomp, source, &source_size, dest, dest, &uncompressed_size, TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    if (decomp_status != TINFL_STATUS_DONE) {
        return decomp_status;
    }
    return 0;
}

/*!
   @brief   Draw a single character to a pre-allocated buffer.
*/
static void IRAM_ATTR draw_char(const GFXfont *font, uint8_t *buffer,
                                int *cursor_x, int cursor_y, uint16_t buf_width,
                                uint16_t buf_height, uint32_t cp,
                                const FontProperties *props) {

  const GFXglyph *glyph;
  get_glyph(font, cp, &glyph);

  if (!glyph) {
    get_glyph(font, props->fallback_glyph, &glyph);
  }

  if (!glyph) {
    return;
  }

  uint32_t offset = glyph->data_offset;
  uint8_t width = glyph->width, height = glyph->height;
  int left = glyph->left;

  int byte_width = (width / 2 + width % 2);
  unsigned long bitmap_size = byte_width * height;
  uint8_t *bitmap = NULL;
  if (font->compressed) {
    bitmap = (uint8_t *)malloc(bitmap_size);
    if (bitmap == NULL && bitmap_size) {
      ESP_LOGE("font", "malloc failed.");
      return;
    }
    uncompress(bitmap, bitmap_size, &font->bitmap[offset],
               glyph->compressed_size);
  } else {
    bitmap = &font->bitmap[offset];
  }

  uint8_t color_lut[16];
  for (int c = 0; c < 16; c++) {
    int color_difference = (int)props->fg_color - (int)props->bg_color;
    color_lut[c] = max(0, min(15, props->bg_color + c * color_difference / 15));
  }

  for (int y = 0; y < height; y++) {
    int yy = cursor_y - glyph->top + y;
    if (yy < 0 || yy >= buf_height) {
      continue;
    }
    int start_pos = *cursor_x + left;
    bool byte_complete = start_pos % 2;
    int x = max(0, -start_pos);
    int max_x = min(start_pos + width, buf_width * 2);
    for (int xx = start_pos; xx < max_x; xx++) {
      uint32_t buf_pos = yy * buf_width + xx / 2;
      uint8_t old = buffer[buf_pos];
      uint8_t bm = bitmap[y * byte_width + x / 2];
      if ((x & 1) == 0) {
        bm = bm & 0xF;
      } else {
        bm = bm >> 4;
      }

      if ((xx & 1) == 0) {
        buffer[buf_pos] = (old & 0xF0) | color_lut[bm];
      } else {
        buffer[buf_pos] = (old & 0x0F) | (color_lut[bm] << 4);
      }
      byte_complete = !byte_complete;
      x++;
    }
  }
  if (font->compressed) {
    free(bitmap);
  }
  *cursor_x += glyph->advance_x;
}

/*!
 * @brief Calculate the bounds of a character when drawn at (x, y), move the
 * cursor (*x) forward, adjust the given bounds.
 */
static void get_char_bounds(const GFXfont *font, uint32_t cp, int *x, int *y,
                            int *minx, int *miny, int *maxx, int *maxy,
                            const FontProperties *props) {
  const GFXglyph *glyph;
  get_glyph(font, cp, &glyph);

  if (!glyph) {
    get_glyph(font, props->fallback_glyph, &glyph);
  }

  if (!glyph) {
    return;
  }

  int x1 = *x + glyph->left, y1 = *y + glyph->top - glyph->height,
      x2 = x1 + glyph->width, y2 = y1 + glyph->height;

  // background needs to be taken into account
  if (props->flags & DRAW_BACKGROUND) {
    *minx = min(*x, min(*minx, x1));
    *maxx = max(max(*x + glyph->advance_x, x2), *maxx);
    *miny = min(*y + font->descender, min(*miny, y1));
    *maxy = max(*y + font->ascender, max(*maxy, y2));
  } else {
    if (x1 < *minx)
      *minx = x1;
    if (y1 < *miny)
      *miny = y1;
    if (x2 > *maxx)
      *maxx = x2;
    if (y2 > *maxy)
      *maxy = y2;
  }
  *x += glyph->advance_x;
}

void get_text_bounds(const GFXfont *font, const char *string, int *x, int *y,
                     int *x1, int *y1, int *w, int *h,
                     const FontProperties *properties) {

  FontProperties props;
  if (properties == NULL) {
    props = font_properties_default();
  } else {
    props = *properties;
  }

  if (*string == '\0') {
    *w = 0;
    *h = 0;
    *y1 = *y;
    *x1 = *x;
    return;
  }
  int minx = 100000, miny = 100000, maxx = -1, maxy = -1;
  int original_x = *x;
  uint32_t c;
  while ((c = next_cp((const uint8_t **)&string))) {
    get_char_bounds(font, c, x, y, &minx, &miny, &maxx, &maxy, &props);
  }
  *x1 = min(original_x, minx);
  *w = maxx - *x1;
  *y1 = miny;
  *h = maxy - miny;
}

void write_mode(const GFXfont *font, const char *string, int *cursor_x,
                int *cursor_y, uint8_t *framebuffer, enum DrawMode mode,
                const FontProperties *properties) {

  if (*string == '\0') {
    return;
  }

  FontProperties props;
  if (properties == NULL) {
    props = font_properties_default();
  } else {
    props = *properties;
  }

  int x1 = 0, y1 = 0, w = 0, h = 0;
  int tmp_cur_x = *cursor_x;
  int tmp_cur_y = *cursor_y;
  get_text_bounds(font, string, &tmp_cur_x, &tmp_cur_y, &x1, &y1, &w, &h,
                  &props);

  // no printable characters
  if (w < 0 || h < 0) {
      return;
  }

  uint8_t *buffer;
  int buf_width;
  int buf_height;
  int baseline_height = *cursor_y - y1;

  // The local cursor position:
  // 0, if drawing to a local temporary buffer
  // the given cursor position, if drawing to a full frame buffer
  int local_cursor_x = 0;
  int local_cursor_y = 0;

  if (framebuffer == NULL) {
    buf_width = (w / 2 + w % 2);
    buf_height = h;
    buffer = (uint8_t *)malloc(buf_width * buf_height);
    memset(buffer, 255, buf_width * buf_height);
    local_cursor_y = buf_height - baseline_height;
  } else {
    buf_width = EPD_WIDTH / 2;
    buf_height = EPD_HEIGHT;
    buffer = framebuffer;
    local_cursor_x = *cursor_x;
    local_cursor_y = *cursor_y;
  }

  uint32_t c;

  int cursor_x_init = local_cursor_x;
  int cursor_y_init = local_cursor_y;

  uint8_t bg = props.bg_color;
  if (props.flags & DRAW_BACKGROUND) {
    for (int l = local_cursor_y - font->ascender;
         l < local_cursor_y - font->descender; l++) {
      epd_draw_hline(local_cursor_x, l, w, bg << 4, buffer);
    }
  }
  while ((c = next_cp((const uint8_t **)&string))) {
    draw_char(font, buffer, &local_cursor_x, local_cursor_y, buf_width,
              buf_height, c, &props);
  }

  *cursor_x += local_cursor_x - cursor_x_init;
  *cursor_y += local_cursor_y - cursor_y_init;

  if (framebuffer == NULL) {
    Rect_t area = {
        .x = x1, .y = *cursor_y - h + baseline_height, .width = w, .height = h};

    epd_draw_image(area, buffer, NULL);

    free(buffer);
  }
}

void writeln(const GFXfont *font, const char *string, int *cursor_x,
             int *cursor_y, uint8_t *framebuffer) {
  return write_mode(font, string, cursor_x, cursor_y, framebuffer,
                    DRAW_DEFAULT, NULL);
}

void write_string(const GFXfont *font, const char *string, int *cursor_x,
                  int *cursor_y, uint8_t *framebuffer) {
  char *token, *newstring, *tofree;
  if (string == NULL) {
    ESP_LOGE("font.c", "cannot draw a NULL string!");
    return;
  }
  tofree = newstring = strdup(string);
  if (newstring == NULL) {
    ESP_LOGE("font.c", "cannot allocate string copy!");
    return;
  }

  // taken from the strsep manpage
  int line_start = *cursor_x;
  while ((token = strsep(&newstring, "\n")) != NULL) {
    *cursor_x = line_start;
    writeln(font, token, cursor_x, cursor_y, framebuffer);
    *cursor_y += font->advance_y;
  }

  free(tofree);
}
