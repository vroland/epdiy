#include "EPD.h"
#include "ed097oc4.h"

#include "esp_heap_caps.h"
#include "xtensa/core-macros.h"
#include <string.h>
#include "esp_assert.h"
#include "esp_types.h"

#define EPD_WIDTH 1200
#define EPD_HEIGHT 825

// number of bytes needed for one line of EPD pixel data.
#define EPD_LINE_BYTES 1200 / 4

// status tracker for row skipping
uint32_t skipping;

#define CLEAR_BYTE 0B10101010
#define DARK_BYTE 0B01010101

/* 4bpp Contrast cycles in order of contrast (Darkest first).  */
const uint8_t contrast_cycles_4[15] = {3, 3, 2, 2, 3,  3,  3, 4,
                                       4, 5, 5, 5, 10, 20, 30};

// Heap space to use for the EPD output lookup table, which
// is calculated for each cycle.
static uint8_t *conversion_lut;

// output a row to the display.
static void write_row(uint32_t output_time_us) {
  skipping = 0;
  epd_output_row(output_time_us);
}

void reorder_line_buffer(uint32_t *line_data);

void epd_init() {
  skipping = 0;
  epd_base_init(EPD_WIDTH);

  conversion_lut = (uint8_t *)heap_caps_malloc(1 << 16, MALLOC_CAP_8BIT);
  assert(conversion_lut != NULL);
}

// skip a display row
void skip_row(uint8_t pipeline_finish_time) {
  // output previously loaded row, fill buffer with no-ops.
  if (skipping == 0) {
    epd_switch_buffer();
    memset(epd_get_current_buffer(), 0, EPD_LINE_BYTES);
    epd_switch_buffer();
    memset(epd_get_current_buffer(), 0, EPD_LINE_BYTES);
    epd_output_row(pipeline_finish_time);
    // avoid tainting of following rows by
    // allowing residual charge to dissipate
    unsigned counts = XTHAL_GET_CCOUNT() + 50 * 240;
    while (XTHAL_GET_CCOUNT() < counts) {
    };
  };
  if (skipping == 1) {
    epd_output_row(1);
  }
  if (skipping > 1) {
    epd_skip();
  }
  skipping++;
}

void epd_push_pixels(Rect_t *area, short time, bool color) {

  uint8_t row[EPD_LINE_BYTES] = {0};

  for (uint32_t i = 0; i < area->width; i++) {
    uint32_t position = i + area->x % 4;
    uint8_t mask =
        (color ? CLEAR_BYTE : DARK_BYTE) & (0b00000011 << (2 * (position % 4)));
    row[area->x / 4 + position / 4] |= mask;
  }
  reorder_line_buffer((uint32_t *)row);

  epd_start_frame();

  for (int i = 0; i < EPD_HEIGHT; i++) {
    // before are of interest: skip
    if (i < area->y) {
      skip_row(time);
      // start area of interest: set row data
    } else if (i == area->y) {
      epd_switch_buffer();
      memcpy(epd_get_current_buffer(), row, EPD_LINE_BYTES);
      epd_switch_buffer();
      memcpy(epd_get_current_buffer(), row, EPD_LINE_BYTES);

      write_row(time);
      // load nop row if done with area
    } else if (i >= area->y + area->height) {
      skip_row(time);
      // output the same as before
    } else {
      write_row(time);
    }
  }
  // Since we "pipeline" row output, we still have to latch out the last row.
  write_row(time);

  epd_end_frame();
}

void epd_clear_area(Rect_t area) {
  const short white_time = 50;
  const short dark_time = 50;

  for (int i = 0; i < 3; i++) {
    epd_push_pixels(&area, dark_time, 0);
  }
  for (int i = 0; i < 3; i++) {
    epd_push_pixels(&area, white_time, 1);
  }
  for (int i = 0; i < 3; i++) {
    epd_push_pixels(&area, white_time, 0);
  }
  for (int i = 0; i < 3; i++) {
    epd_push_pixels(&area, white_time, 1);
  }
  for (int i = 0; i < 3; i++) {
    epd_push_pixels(&area, white_time, 0);
  }
  for (int i = 0; i < 3; i++) {
    epd_push_pixels(&area, white_time, 1);
  }
}

Rect_t epd_full_screen() {
  Rect_t area = {.x = 0, .y = 0, .width = EPD_WIDTH, .height = EPD_HEIGHT};
  return area;
}

void epd_clear() { epd_clear_area(epd_full_screen()); }

/*
 * Reorder the output buffer to account for I2S FIFO order.
 */
void reorder_line_buffer(uint32_t *line_data) {
  for (uint32_t i = 0; i < EPD_LINE_BYTES / 4; i++) {
    uint32_t val = *line_data;
    *(line_data++) = val >> 16 | ((val & 0x0000FFFF) << 16);
  }
}

void IRAM_ATTR calc_epd_input_4bpp(uint32_t *line_data, uint8_t *epd_input,
                                   uint8_t k, uint8_t *conversion_lut) {

  uint32_t *wide_epd_input = (uint32_t *)epd_input;
  uint16_t *line_data_16 = (uint16_t *)line_data;

  // this is reversed for little-endian, but this is later compensated
  // through the output peripheral.
  for (uint32_t j = 0; j < EPD_WIDTH / 16; j++) {

    uint16_t v1 = *(line_data_16++);
    uint16_t v2 = *(line_data_16++);
    uint16_t v3 = *(line_data_16++);
    uint16_t v4 = *(line_data_16++);
    uint32_t pixel = conversion_lut[v1] << 16 | conversion_lut[v2] << 24 |
                     conversion_lut[v3] | conversion_lut[v4] << 8;
    wide_epd_input[j] = pixel;
  }
}

void IRAM_ATTR populate_LUT(uint8_t *lut_mem, uint8_t k) {
  const uint32_t shiftmul = (1 << 15) + (1 << 21) + (1 << 3) + (1 << 9);

  uint8_t r = k + 1;
  uint32_t add_mask = (r << 24) | (r << 16) | (r << 8) | r;

  for (uint32_t i = 0; i < (1 << 16); i++) {
    uint32_t val = i;
    val = (val | (val << 8)) & 0x00FF00FF;
    val = (val | (val << 4)) & 0x0F0F0F0F;
    val += add_mask;
    val = ~val;
    // now the bits we need are masked
    val &= 0x10101010;
    // shift relevant bits to the most significant byte, then shift down
    lut_mem[i] = ((val * shiftmul) >> 25);
  }
}

void IRAM_ATTR nibble_shift_buffer_right(uint8_t *buf, uint32_t len) {
  uint8_t carry = 0xF;
  for (uint32_t i = 0; i < len; i++) {
    uint8_t val = buf[i];
    buf[i] = (val << 4) | carry;
    carry = (val & 0xF0) >> 4;
  }
}

inline uint32_t min(uint32_t x, uint32_t y) { return x < y ? x : y; }

void IRAM_ATTR epd_draw_grayscale_image(Rect_t area, uint8_t *data) {
  uint8_t line[EPD_WIDTH / 2];
  memset(line, 255, EPD_WIDTH / 2);
  uint8_t frame_count = 15;
  const uint8_t *contrast_lut = contrast_cycles_4;

  for (uint8_t k = 0; k < frame_count; k++) {
    populate_LUT(conversion_lut, k);
    uint8_t *ptr = data;
    epd_start_frame();

    // initialize with null row to avoid artifacts
    for (int i = 0; i < EPD_HEIGHT; i++) {
      if (i < area.y || i > area.y + area.height) {
        skip_row(contrast_lut[k]);
        continue;
      }

      uint32_t *lp;
      if (area.width == EPD_WIDTH) {
        lp = (uint32_t *)ptr;
        ptr += EPD_WIDTH / 2;
      } else {
        uint8_t *buf_start = line + area.x / 2;
        uint32_t line_bytes = area.width / 2 + area.width % 2;
        memcpy(buf_start, ptr, line_bytes);
        ptr += line_bytes;

        // mask last nibble for uneven width
        if (area.width % 2) {
          *(buf_start + line_bytes - 1) |= 0xF0;
        }
        if (area.x % 2 == 1) {
          // shift one nibble to right
          nibble_shift_buffer_right(
              buf_start, min(line_bytes + 1, (uint32_t)line + EPD_WIDTH / 2 -
                                                 (uint32_t)buf_start));
        }
        lp = (uint32_t *)line;
      }

      uint8_t *buf = epd_get_current_buffer();
      calc_epd_input_4bpp(lp, buf, k, conversion_lut);
      write_row(contrast_lut[k]);
    }
    // Since we "pipeline" row output, we still have to latch out the last row.
    write_row(contrast_lut[k]);
    epd_end_frame();
  }
}
