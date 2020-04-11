#include "EPD.h"
#include "ed097oc4.h"

#include "xtensa/core-macros.h"
#include <string.h>

#define EPD_WIDTH 1200
#define EPD_HEIGHT 825

// number of bytes needed for one line of EPD pixel data.
#define EPD_LINE_BYTES 1200 / 4

// A row with only null bytes, to be loaded when skipping lines
// to avoid slight darkening / lightening.
uint8_t null_row[EPD_LINE_BYTES] = {0};

// status tracker for row skipping
uint32_t skipping;

#define CLEAR_BYTE 0B10101010
#define DARK_BYTE 0B01010101

/* 4bpp Contrast cycles in order of contrast (Darkest first).  */
const uint8_t contrast_cycles_4[15] = {2, 2, 2, 2, 3,  3,  3, 4,
                                       4, 5, 5, 5, 10, 30, 50};

/* 2bpp Contrast cycles in order of contrast (Darkest first).  */
const uint8_t contrast_cycles_2[3] = {8, 10, 100};

// output a row to the display.
static void write_row(uint32_t output_time_us) {
  skipping = 0;
  epd_output_row(output_time_us);
}

/**
 * Convert an 8 bit bitmap image to a linearized unary representation,
 * which is optimized for linear access for displaying.
 *
 * Image width must be divisible by 8.
 */
void img_8bit_to_unary_image(uint8_t *dst, uint8_t *src, uint32_t image_width,
                             uint32_t image_height) {

  uint16_t* dst_16 = (uint16_t*)dst;
  const uint32_t shiftmul = (1 << 24) + (1 << 17) + (1 << 10) + (1 << 3);

  for (uint8_t layer = 0; layer < 15; layer++) {
    uint32_t *batch_ptr = (uint32_t *)src;
    uint8_t k = layer + 1;
    uint32_t add_mask = (k << 24) | (k << 16) | (k << 8) | k;

    for (uint32_t i = 0; i < image_width * image_height / 4 / 4; i++) {

        uint32_t val = *(batch_ptr++);
        val = (val & 0xF0F0F0F0) >> 4;
        val += add_mask;
        // now the bits we need are masked
        val &= 0x10101010;
        // shift relevant bits to the most significant byte, then shift down
        uint16_t pixel = ((val * shiftmul) >> 20) & 0x0F00;

        val = *(batch_ptr++);
        val = (val & 0xF0F0F0F0) >> 4;
        val += add_mask;
        // now the bits we need are masked
        val &= 0x10101010;
        // shift relevant bits to the most significant byte, then shift down
        pixel |= ((val * shiftmul) >> 16) & 0xF000;

        val = *(batch_ptr++);
        val = (val & 0xF0F0F0F0) >> 4;
        val += add_mask;
        // now the bits we need are masked
        val &= 0x10101010;
        // shift relevant bits to the most significant byte, then shift down
        pixel |= ((val * shiftmul) >> 28);

        val = *(batch_ptr++);
        val = (val & 0xF0F0F0F0) >> 4;
        val += add_mask;
        // now the bits we need are masked
        val &= 0x10101010;
        // shift relevant bits to the most significant byte, then shift down
        pixel |= ((val * shiftmul) >> 24) & 0x00F0;
        *(dst_16++) = ~pixel;
    }
  }
}

static inline uint32_t min(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

void IRAM_ATTR draw_image_unary_coded(Rect_t area, uint8_t *data) {

  const uint32_t cache_size = EPD_WIDTH / 8 * 300;

  uint16_t* data_16 = (uint16_t*)data;

  for (int layer = 0; layer < 15; layer++) {

    epd_start_frame();

    uint16_t* cache_ptr;
    for (int i = 0; i < EPD_HEIGHT; i++) {

      uint32_t *buffer = epd_get_current_buffer();

      for (int j = 0; j < EPD_WIDTH / 16; j++) {
        uint32_t x = (uint32_t) * (data_16++);
        // inspired from
        // https://graphics.stanford.edu/~seander/bithacks.html#InterleaveBMN
        x = (x | (x << 8)) & 0x00FF00FF;
        x = (x | (x << 4)) & 0x0F0F0F0F;
        x = (x | (x << 2)) & 0x33333333;
        x = (x | (x << 1)) & 0x55555555;
        *(buffer++) = x;
      }
      // Since we "pipeline" row output, we still have to latch out the last
      // row.
      write_row(contrast_cycles_4[layer]);
    }
    epd_end_frame();
  }
}

void reorder_line_buffer(uint32_t *line_data);

void epd_init() {
  skipping = 0;
  epd_base_init(EPD_WIDTH);
}

// skip a display row
void skip_row() {
  // 2, to latch out previously loaded null row
  if (skipping < 2) {
    memcpy(epd_get_current_buffer(), null_row, EPD_LINE_BYTES);
    epd_switch_buffer();
    memcpy(epd_get_current_buffer(), null_row, EPD_LINE_BYTES);
    epd_output_row(10);
    // avoid tainting of following rows by
    // allowing residual charge to dissipate
    unsigned counts = XTHAL_GET_CCOUNT() + 50 * 240;
    while (XTHAL_GET_CCOUNT() < counts) {
    };
  } else {
    epd_skip();
  }
  skipping++;
}

void epd_draw_byte(Rect_t *area, short time, uint8_t byte) {

  volatile uint8_t *row = (uint8_t *)malloc(EPD_LINE_BYTES);
  for (int i = 0; i < EPD_LINE_BYTES; i++) {
    if (i * 4 + 3 < area->x || i * 4 >= area->x + area->width) {
      row[i] = 0;
    } else {
      // undivisible pixel values
      if (area->x > i * 4) {
        row[i] = byte & (0B11111111 >> (2 * (area->x % 4)));
      } else if (i * 4 + 4 > area->x + area->width) {
        row[i] = byte & (0B11111111 << (8 - 2 * ((area->x + area->width) % 4)));
      } else {
        row[i] = byte;
      }
    }
  }
  reorder_line_buffer((uint32_t *)row);

  epd_start_frame();

  for (int i = 0; i < EPD_HEIGHT; i++) {
    // before are of interest: skip
    if (i < area->y) {
      skip_row();
      // start area of interest: set row data
    } else if (i == area->y) {
      memcpy(epd_get_current_buffer(), row, EPD_LINE_BYTES);
      epd_switch_buffer();
      memcpy(epd_get_current_buffer(), row, EPD_LINE_BYTES);

      write_row(time);
      // load nop row if done with area
    } else if (i >= area->y + area->height) {
      skip_row();
      // output the same as before
    } else {
      write_row(time);
    }
  }
  // Since we "pipeline" row output, we still have to latch out the last row.
  write_row(time);

  epd_end_frame();
  free(row);
}

void epd_clear_area(Rect_t area) {
  const short white_time = 50;
  const short dark_time = 50;

  for (int i = 0; i < 3; i++) {
    epd_draw_byte(&area, dark_time, DARK_BYTE);
  }
  for (int i = 0; i < 3; i++) {
    epd_draw_byte(&area, white_time, CLEAR_BYTE);
  }
  for (int i = 0; i < 3; i++) {
    epd_draw_byte(&area, white_time, DARK_BYTE);
  }
  for (int i = 0; i < 3; i++) {
    epd_draw_byte(&area, white_time, CLEAR_BYTE);
  }
  for (int i = 0; i < 3; i++) {
    epd_draw_byte(&area, white_time, DARK_BYTE);
  }
  for (int i = 0; i < 3; i++) {
    epd_draw_byte(&area, white_time, CLEAR_BYTE);
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

void IRAM_ATTR calc_epd_input_4bpp(uint32_t *line_data,
                                   volatile uint8_t *epd_input, uint8_t k) {

  const uint32_t shiftmul = (1 << 15) + (1 << 21) + (1 << 3) + (1 << 9);

  k = 16 - k;
  uint32_t add_mask = (k << 24) | (k << 16) | (k << 8) | k;
  uint32_t *wide_epd_input = (uint32_t *)epd_input;

  // this is reversed for little-endian, but this is later compensated
  // through the output peripheral.
  for (uint32_t j = 0; j < EPD_WIDTH / 4 / 4; j++) {

    uint32_t pixel =
        (DARK_BYTE << 24) | (DARK_BYTE << 16) | (DARK_BYTE << 8) | DARK_BYTE;
    uint32_t val = *(line_data++);
    val = (val & 0xF0F0F0F0) >> 4;
    val += add_mask;
    // now the bits we need are masked
    val &= 0x10101010;
    // shift relevant bits to the most significant byte, then shift down
    pixel |= ((val * shiftmul) >> 8) & 0x00FF0000;

    val = *(line_data++);
    val = (val & 0xF0F0F0F0) >> 4;
    val += add_mask;
    // now the bits we need are masked
    val &= 0x10101010;
    // shift relevant bits to the most significant byte, then shift down
    pixel |= ((val * shiftmul) >> 0) & 0xFF000000;

    val = *(line_data++);
    val = (val & 0xF0F0F0F0) >> 4;
    val += add_mask;
    // now the bits we need are masked
    val &= 0x10101010;
    // shift relevant bits to the most significant byte, then shift down
    pixel |= ((val * shiftmul) >> 24);

    val = *(line_data++);
    val = (val & 0xF0F0F0F0) >> 4;
    val += add_mask;
    // now the bits we need are masked
    val &= 0x10101010;
    // shift relevant bits to the most significant byte, then shift down
    pixel |= ((val * shiftmul) >> 16) & 0x0000FF00;
    wide_epd_input[j] = pixel;
  }
}

void IRAM_ATTR calc_epd_input_2bpp(uint32_t *line_data,
                                   volatile uint8_t *epd_input, uint8_t k) {

  const uint32_t shiftmul = (1 << 17) + (1 << 23) + (1 << 5) + (1 << 11);

  k = 4 - k;
  uint32_t add_mask = (k << 24) | (k << 16) | (k << 8) | k;

  // this is reversed for little-endian, but this is later compensated
  // through the output peripheral.
  for (uint32_t j = 0; j < EPD_WIDTH / 4; j++) {

    uint8_t pixel = DARK_BYTE;
    uint32_t val = *(line_data++);
    val = (val & 0xC0C0C0C0) >> 6;
    val += add_mask;
    // now the bits we need are masked
    val &= 0x04040404;
    // shift relevant bits to the most significant byte, then shift down
    pixel |= (val * shiftmul) >> 24;
    epd_input[j] = pixel;
  }
  reorder_line_buffer((uint32_t *)epd_input);
}

void IRAM_ATTR epd_draw_picture(Rect_t area, uint8_t *data, EPDBitdepth_t bpp) {
  uint8_t row[EPD_WIDTH]; // = (uint8_t *)heap_caps_malloc(EPD_LINE_BYTES,
                          // MALLOC_CAP_8BIT);
  uint32_t
      line[EPD_WIDTH / 4]; //*line = (uint32_t *)heap_caps_malloc(EPD_WIDTH,
                           // MALLOC_CAP_32BIT);

  uint8_t frame_count = (1 << bpp) - 1;
  const uint8_t *contrast_lut;
  switch (bpp) {
  case BIT_DEPTH_4:
    contrast_lut = contrast_cycles_4;
    break;
  case BIT_DEPTH_2:
    contrast_lut = contrast_cycles_2;
    break;
  default:
    assert("invalid grayscale mode!");
    return;
  };

  for (uint8_t k = frame_count; k > 0; k--) {
    uint8_t *ptr = data;
    epd_start_frame();

    // initialize with null row to avoid artifacts
    for (int i = 0; i < EPD_HEIGHT; i++) {
      if (i < area.y || i >= area.y + area.height) {
        skip_row();
        continue;
      }

      if (area.width == EPD_WIDTH) {
        // volatile uint32_t t = micros();
        memcpy(line, (uint32_t *)ptr, EPD_WIDTH);
        // volatile uint32_t t2 = micros();
        // printf("copy took %d us.\n", t2 - t);
        ptr += EPD_WIDTH;
      } else {
        memset(line, 255, EPD_WIDTH);
        uint8_t *buf_start = ((uint8_t *)line) + area.x;
        memcpy(buf_start, ptr, area.width);
        ptr += area.width;
      }
      uint32_t *lp = line;

      volatile uint8_t *buf = epd_get_current_buffer();
      switch (bpp) {
      case BIT_DEPTH_4: {
        calc_epd_input_4bpp(lp, buf, k);
        break;
      }
      case BIT_DEPTH_2:
        calc_epd_input_2bpp(lp, buf, k);
        break;
      default:
        assert("invalid grayscale mode!");
      };
      write_row(contrast_lut[frame_count - k]);
    }
    // Since we "pipeline" row output, we still have to latch out the last row.
    write_row(contrast_lut[frame_count - k]);
    epd_end_frame();
  }
  // free(row);
  // free(line);
}
