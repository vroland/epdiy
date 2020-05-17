#include "epd_driver.h"
#include "ed097oc4.h"
#include "epd_temperature.h"

#include "esp_assert.h"
#include "esp_heap_caps.h"
#include "esp_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "xtensa/core-macros.h"
#include <string.h>
#include "waveform_gc4.h"

// number of bytes needed for one line of EPD pixel data.
#define EPD_LINE_BYTES EPD_WIDTH / 4

// status tracker for row skipping
uint32_t skipping;

#define CLEAR_BYTE 0B10101010
#define DARK_BYTE 0B01010101

#if defined(CONFIG_EPD_DISPLAY_TYPE_ED097OC4) || defined(CONFIG_EPD_DISPLAY_TYPE_ED060SC4)
/* 4bpp Contrast cycles in order of contrast (Darkest first).  */
const uint8_t contrast_cycles_4[15] = {30, 30, 20, 20, 30,  30,  30, 40,
                                       40, 50, 50, 50, 100, 200, 300};
#elif defined(CONFIG_EPD_DISPLAY_TYPE_ED097TC2)
const uint8_t contrast_cycles_4[15] = {15, 8, 8, 8, 8,  8,  10, 10,
                                       10, 10, 20, 20, 50, 100, 200};
#else
#error "no display type defined!"
#endif

// Heap space to use for the EPD output lookup table, which
// is calculated for each cycle.
static uint32_t* lines[EPD_HEIGHT];

static uint8_t* ext_lut;

// DEPRECATED
static uint8_t* conversion_lut;

// output a row to the display.
static void write_row(uint32_t output_time_dus) {
  skipping = 0;
  epd_output_row(output_time_dus);
}

void reorder_line_buffer(uint32_t *line_data);

void epd_init() {
  skipping = 0;
  epd_base_init(EPD_WIDTH);
  epd_temperature_init();

  ext_lut = (uint8_t *)heap_caps_malloc(1 << 16, MALLOC_CAP_8BIT);
  assert(ext_lut != NULL);

  for (int i=0; i < EPD_HEIGHT; i++) {
    lines[i] = heap_caps_malloc(EPD_WIDTH / 4, MALLOC_CAP_32BIT);
    if (lines[i] == NULL) {
       ESP_LOGE("epd_task", "Could not allocate line buffer!");
    }
  }
}

// skip a display row
void skip_row(uint8_t pipeline_finish_time) {
  // output previously loaded row, fill buffer with no-ops.
  if (skipping == 0) {
    epd_switch_buffer();
    memset(epd_get_current_buffer(), 255, EPD_LINE_BYTES);
    epd_switch_buffer();
    memset(epd_get_current_buffer(), 255, EPD_LINE_BYTES);
    epd_output_row(pipeline_finish_time);
    // avoid tainting of following rows by
    // allowing residual charge to dissipate
    unsigned counts = XTHAL_GET_CCOUNT() + 50 * 240;
    while (XTHAL_GET_CCOUNT() < counts) {
    };
  };
  if (skipping == 1) {
    epd_output_row(10);
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

      write_row(time * 10);
      // load nop row if done with area
    } else if (i >= area->y + area->height) {
      skip_row(time);
      // output the same as before
    } else {
      write_row(time * 10);
    }
  }
  // Since we "pipeline" row output, we still have to latch out the last row.
  write_row(time * 10);

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

void build_ext_lut(int frame) {
  for (uint32_t x=0; x < 256; x++) {
    for (uint32_t y=0; y < 256; y++) {
      uint8_t val1 = waveform_gc4[frame][x & 0xF][y & 0xF];
      uint8_t val2 = waveform_gc4[frame][(x & 0xF0) >> 4][(y & 0xF0) >> 4];
      ext_lut[(x << 8) | y] = (val2 << 2) | val1;
    }
  }
}

typedef struct {
  uint8_t* from;
  uint8_t* to;
  int h_min;
  int h_max;
  TaskHandle_t main_task;
} output_calc_params_t;

void IRAM_ATTR build_difference_image(output_calc_params_t* params) {
    uint8_t* input1_ptr = params->to + (EPD_WIDTH / 2 * params->h_min);
    uint8_t* input2_ptr = params->from + (EPD_WIDTH / 2 * params->h_min);
    for (int y=params->h_min; y < params->h_max; y+=2) {
        uint8_t input1[EPD_WIDTH / 2];
        uint8_t input2[EPD_WIDTH / 2];
        memcpy(input1, input1_ptr, sizeof(input1));
        memcpy(input2, input2_ptr, sizeof(input2));
        uint8_t* t_ptr = input1;
        uint8_t* f_ptr = input2;
        for (int x=0; x < EPD_WIDTH / 16; x++) {
            uint32_t val = 0;
            uint16_t t;
            uint16_t f;
            t = *(t_ptr++);
            f = *(f_ptr++);
            val |= ext_lut[(f << 8) | t] << 28;
            t = *(t_ptr++);
            f = *(f_ptr++);
            val |= ext_lut[(f << 8) | t] << 24;
            t = *(t_ptr++);
            f = *(f_ptr++);
            val |= ext_lut[(f << 8) | t] << 20;
            t = *(t_ptr++);
            f = *(f_ptr++);
            val |= ext_lut[(f << 8) | t] << 16;
            t = *(t_ptr++);
            f = *(f_ptr++);
            val |= ext_lut[(f << 8) | t] << 12;
            t = *(t_ptr++);
            f = *(f_ptr++);
            val |= ext_lut[(f << 8) | t] << 8;
            t = *(t_ptr++);
            f = *(f_ptr++);
            val |= ext_lut[(f << 8) | t] << 4;
            t = *(t_ptr++);
            f = *(f_ptr++);
            val |= ext_lut[(f << 8) | t];
            lines[y][x] = val;
        }
        input1_ptr += EPD_WIDTH / 2 * 2;
        input2_ptr += EPD_WIDTH / 2 * 2;
    }
    xTaskNotifyGive(params->main_task);
    vTaskDelete(NULL);
}

void IRAM_ATTR calculate_output(uint8_t* from, uint8_t* to, int frame) {
  build_ext_lut(frame);
  TaskHandle_t t1, t2, parent;
  parent = xTaskGetCurrentTaskHandle();
  output_calc_params_t p1 = {
    .from = from,
    .to = to,
    .h_min = 0,
    .h_max = EPD_HEIGHT,
    .main_task = parent,
  };
  output_calc_params_t p2 = {
    .from = from,
    .to = to,
    .h_min = 1,
    .h_max = EPD_HEIGHT,
    .main_task = parent,
  };
  xTaskCreate((void(*)(void*))build_difference_image, "calc_out1", 8000, &p1, 10, &t1);
  xTaskCreate((void(*)(void*))build_difference_image, "calc_out2", 8000, &p2, 10, &t2);
  ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
  ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
}

void IRAM_ATTR populate_LUT(uint8_t *lut_mem, uint8_t k, enum DrawMode mode) {
  const uint32_t shiftmul = (1 << 15) + (1 << 21) + (1 << 3) + (1 << 9);

  uint8_t r = k + 1;
  uint32_t add_mask = (r << 24) | (r << 16) | (r << 8) | r;
  uint8_t shift_amount;
  switch (mode) {
    case BLACK_ON_WHITE:
      shift_amount = 25;
      break;
    case WHITE_ON_WHITE:
      shift_amount = 24;
      break;
    default:
      ESP_LOGW("epd_driver", "unknown draw mode %d!", mode);
      shift_amount = 25;
      break;
  }

  for (uint32_t i = 0; i < (1 << 16); i++) {
    uint32_t val = i;
    val = (val | (val << 8)) & 0x00FF00FF;
    val = (val | (val << 4)) & 0x0F0F0F0F;
    val += add_mask;
    val = ~val;
    // now the bits we need are masked
    val &= 0x10101010;
    // shift relevant bits to the most significant byte, then shift down
    lut_mem[i] = ((val * shiftmul) >> shift_amount);
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

void epd_draw_hline(int x, int y, int length, uint8_t color,
                    uint8_t *framebuffer) {
  if (y < 0 || y >= EPD_HEIGHT) {
    return;
  }
  for (int i = 0; i < length; i++) {
    int xx = x + i;
    if (xx < 0 || xx >= EPD_WIDTH) {
      continue;
    }
    uint8_t *buf_ptr = &framebuffer[y * EPD_WIDTH / 2 + xx / 2];
    if (xx % 2) {
      *buf_ptr = (*buf_ptr & 0x0F) | (color & 0xF0);
    } else {
      *buf_ptr = (*buf_ptr & 0xF0) | (color >> 4);
    }
  }
}

void epd_draw_vline(int x, int y, int length, uint8_t color,
                    uint8_t *framebuffer) {
  if (x < 0 || x >= EPD_WIDTH) {
    return;
  }
  for (int i = 0; i < length; i++) {
    int yy = y + i;
    if (yy < 0 || yy >= EPD_HEIGHT) {
      return;
    }
    uint8_t *buf_ptr = &framebuffer[yy * EPD_WIDTH / 2 + x / 2];
    if (x % 2) {
      *buf_ptr = (*buf_ptr & 0x0F) | (color & 0xF0);
    } else {
      *buf_ptr = (*buf_ptr & 0xF0) | (color >> 4);
    }
  }
}

void epd_copy_to_framebuffer(Rect_t image_area, uint8_t *image_data,
                             uint8_t *framebuffer) {

  assert(framebuffer != NULL);

  for (uint32_t i = 0; i < image_area.width * image_area.height; i++) {

    uint32_t value_index = i;
    // for images of uneven width,
    // consume an additional nibble per row.
    if (image_area.width % 2) {
      value_index += i / image_area.width;
    }
    uint8_t val = (value_index % 2) ? (image_data[value_index / 2] & 0xF0) >> 4
                                    : image_data[value_index / 2] & 0x0F;

    int xx = image_area.x + i % image_area.width;
    if (xx < 0 || xx >= EPD_WIDTH) {
      continue;
    }
    int yy = image_area.y + i / image_area.width;
    if (yy < 0 || yy >= EPD_HEIGHT) {
      continue;
    }
    uint8_t *buf_ptr = &framebuffer[yy * EPD_WIDTH / 2 + xx / 2];
    if (xx % 2) {
      *buf_ptr = (*buf_ptr & 0x0F) | (val << 4);
    } else {
      *buf_ptr = (*buf_ptr & 0xF0) | val;
    }
  }
}

void IRAM_ATTR epd_draw_grayscale_image(Rect_t area, uint8_t *data) {
  epd_draw_image(area, data, BLACK_ON_WHITE);
}

void epd_change_image(Rect_t area, uint8_t* from, uint8_t* to) {
  for (uint8_t k = 0; k < 16; k++) {
    uint32_t t1 = esp_timer_get_time();
    calculate_output(from, to, k);
    uint32_t t2 = esp_timer_get_time();
    printf("generate took %d.\n", t2 - t1);
    epd_start_frame();
    for (int i = 0; i < EPD_HEIGHT; i++) {
      memcpy(epd_get_current_buffer(), lines[i], EPD_LINE_BYTES);
      write_row(10);
    }
    epd_end_frame();
  }
}

void IRAM_ATTR epd_draw_image(Rect_t area, uint8_t *data, enum DrawMode mode) {
  uint8_t line[EPD_WIDTH / 2];
  memset(line, 255, EPD_WIDTH / 2);
  uint8_t frame_count = 15;
  const uint8_t *contrast_lut = contrast_cycles_4;

  for (uint8_t k = 0; k < frame_count; k++) {
    populate_LUT(conversion_lut, k, mode);
    uint8_t *ptr = data;

    if (area.x < 0) {
      ptr += -area.x / 2;
    }
    if (area.y < 0) {
      ptr += (area.width / 2 + area.width % 2) * -area.y;
    }


    epd_start_frame();
    // initialize with null row to avoid artifacts
    for (int i = 0; i < EPD_HEIGHT; i++) {
      if (i < area.y || i >= area.y + area.height) {
        skip_row(contrast_lut[k]);
        continue;
      }

      uint32_t *lp;
      if (area.width == EPD_WIDTH && area.x == 0) {
        lp = (uint32_t *)ptr;
        ptr += EPD_WIDTH / 2;
      } else {
        uint8_t *buf_start = (uint8_t *)line;
        uint32_t line_bytes = area.width / 2 + area.width % 2;
        if (area.x >= 0) {
          buf_start += area.x / 2;
        } else {
          // reduce line_bytes to actually used bytes
          line_bytes += area.x / 2;
        }
        line_bytes = min(line_bytes, EPD_WIDTH / 2 - (uint32_t)(buf_start - line));
        memcpy(buf_start, ptr, line_bytes);
        ptr += area.width / 2 + area.width % 2;

        // mask last nibble for uneven width
        if (area.width % 2 == 1 && area.x / 2 + area.width / 2 + 1 < EPD_WIDTH) {
          *(buf_start + line_bytes - 1) |= 0xF0;
        }
        if (area.x % 2 == 1 && area.x < EPD_WIDTH) {
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
