#include "epd_driver.h"
#include "ed097oc4.h"
#include "epd_temperature.h"

#include "esp_assert.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "xtensa/core-macros.h"
#include <string.h>

// number of bytes needed for one line of EPD pixel data.
#define EPD_LINE_BYTES EPD_WIDTH / 4

// status tracker for row skipping
uint32_t skipping;

#define CLEAR_BYTE 0B10101010
#define DARK_BYTE 0B01010101

#if defined(CONFIG_EPD_DISPLAY_TYPE_ED097OC4) ||                               \
    defined(CONFIG_EPD_DISPLAY_TYPE_ED060SC4)
/* 4bpp Contrast cycles in order of contrast (Darkest first).  */
const int contrast_cycles_4[15] = {30, 30, 20, 20, 30,  30,  30, 40,
                                   40, 50, 50, 50, 100, 200, 300};

const int contrast_cycles_4_white[15] = {30, 30, 20, 20, 30,  30,  30, 40,
                                         40, 50, 50, 50, 100, 200, 300};

#elif defined(CONFIG_EPD_DISPLAY_TYPE_ED097TC2)
const int contrast_cycles_4[15] = {15, 8,  8,  8,  8,  8,   10, 10,
                                   10, 10, 20, 20, 50, 100, 200};

const int contrast_cycles_4_white[15] = {7, 8, 8, 6, 6, 6,  6,  6,
                                         6, 6, 6, 8, 8, 10, 100};
#else
#error "no display type defined!"
#endif

// Heap space to use for the EPD output lookup table, which
// is calculated for each cycle.
static uint8_t *conversion_lut;
static QueueHandle_t output_queue;

// output a row to the display.
static void write_row(uint32_t output_time_dus) {
  // avoid too light output after skipping on some displays
  if (skipping) {
    vTaskDelay(2);
  }
  skipping = 0;
  epd_output_row(output_time_dus);
}

void reorder_line_buffer(uint32_t *line_data);

void epd_init() {
  skipping = 0;
  epd_base_init(EPD_WIDTH);
  epd_temperature_init();

  conversion_lut = (uint8_t *)heap_caps_malloc(1 << 16, MALLOC_CAP_8BIT);
  assert(conversion_lut != NULL);
  output_queue = xQueueCreate(64, EPD_WIDTH / 2);
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
    epd_output_row(10);
  }
  if (skipping > 1) {
    epd_skip();
  }
  skipping++;
}

void epd_push_pixels(Rect_t area, short time, int color) {

  uint8_t row[EPD_LINE_BYTES] = {0};

  for (uint32_t i = 0; i < area.width; i++) {
    uint32_t position = i + area.x % 4;
    uint8_t mask =
        (color ? CLEAR_BYTE : DARK_BYTE) & (0b00000011 << (2 * (position % 4)));
    row[area.x / 4 + position / 4] |= mask;
  }
  reorder_line_buffer((uint32_t *)row);

  epd_start_frame();

  for (int i = 0; i < EPD_HEIGHT; i++) {
    // before are of interest: skip
    if (i < area.y) {
      skip_row(time);
      // start area of interest: set row data
    } else if (i == area.y) {
      epd_switch_buffer();
      memcpy(epd_get_current_buffer(), row, EPD_LINE_BYTES);
      epd_switch_buffer();
      memcpy(epd_get_current_buffer(), row, EPD_LINE_BYTES);

      write_row(time * 10);
      // load nop row if done with area
    } else if (i >= area.y + area.height) {
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

void epd_clear_area(Rect_t area) { epd_clear_area_cycles(area, 3, 50); }

void epd_clear_area_cycles(Rect_t area, int cycles, int cycle_time) {
  const short white_time = cycle_time;
  const short dark_time = cycle_time;

  for (int c = 0; c < cycles; c++) {
    for (int i = 0; i < 3; i++) {
      epd_push_pixels(area, dark_time, 0);
    }
    for (int i = 0; i < 3; i++) {
      epd_push_pixels(area, white_time, 1);
    }
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

const DRAM_ATTR uint32_t lut_1bpp[256] = {
    0x0000, 0x0001, 0x0004, 0x0005, 0x0010, 0x0011, 0x0014, 0x0015, 0x0040,
    0x0041, 0x0044, 0x0045, 0x0050, 0x0051, 0x0054, 0x0055, 0x0100, 0x0101,
    0x0104, 0x0105, 0x0110, 0x0111, 0x0114, 0x0115, 0x0140, 0x0141, 0x0144,
    0x0145, 0x0150, 0x0151, 0x0154, 0x0155, 0x0400, 0x0401, 0x0404, 0x0405,
    0x0410, 0x0411, 0x0414, 0x0415, 0x0440, 0x0441, 0x0444, 0x0445, 0x0450,
    0x0451, 0x0454, 0x0455, 0x0500, 0x0501, 0x0504, 0x0505, 0x0510, 0x0511,
    0x0514, 0x0515, 0x0540, 0x0541, 0x0544, 0x0545, 0x0550, 0x0551, 0x0554,
    0x0555, 0x1000, 0x1001, 0x1004, 0x1005, 0x1010, 0x1011, 0x1014, 0x1015,
    0x1040, 0x1041, 0x1044, 0x1045, 0x1050, 0x1051, 0x1054, 0x1055, 0x1100,
    0x1101, 0x1104, 0x1105, 0x1110, 0x1111, 0x1114, 0x1115, 0x1140, 0x1141,
    0x1144, 0x1145, 0x1150, 0x1151, 0x1154, 0x1155, 0x1400, 0x1401, 0x1404,
    0x1405, 0x1410, 0x1411, 0x1414, 0x1415, 0x1440, 0x1441, 0x1444, 0x1445,
    0x1450, 0x1451, 0x1454, 0x1455, 0x1500, 0x1501, 0x1504, 0x1505, 0x1510,
    0x1511, 0x1514, 0x1515, 0x1540, 0x1541, 0x1544, 0x1545, 0x1550, 0x1551,
    0x1554, 0x1555, 0x4000, 0x4001, 0x4004, 0x4005, 0x4010, 0x4011, 0x4014,
    0x4015, 0x4040, 0x4041, 0x4044, 0x4045, 0x4050, 0x4051, 0x4054, 0x4055,
    0x4100, 0x4101, 0x4104, 0x4105, 0x4110, 0x4111, 0x4114, 0x4115, 0x4140,
    0x4141, 0x4144, 0x4145, 0x4150, 0x4151, 0x4154, 0x4155, 0x4400, 0x4401,
    0x4404, 0x4405, 0x4410, 0x4411, 0x4414, 0x4415, 0x4440, 0x4441, 0x4444,
    0x4445, 0x4450, 0x4451, 0x4454, 0x4455, 0x4500, 0x4501, 0x4504, 0x4505,
    0x4510, 0x4511, 0x4514, 0x4515, 0x4540, 0x4541, 0x4544, 0x4545, 0x4550,
    0x4551, 0x4554, 0x4555, 0x5000, 0x5001, 0x5004, 0x5005, 0x5010, 0x5011,
    0x5014, 0x5015, 0x5040, 0x5041, 0x5044, 0x5045, 0x5050, 0x5051, 0x5054,
    0x5055, 0x5100, 0x5101, 0x5104, 0x5105, 0x5110, 0x5111, 0x5114, 0x5115,
    0x5140, 0x5141, 0x5144, 0x5145, 0x5150, 0x5151, 0x5154, 0x5155, 0x5400,
    0x5401, 0x5404, 0x5405, 0x5410, 0x5411, 0x5414, 0x5415, 0x5440, 0x5441,
    0x5444, 0x5445, 0x5450, 0x5451, 0x5454, 0x5455, 0x5500, 0x5501, 0x5504,
    0x5505, 0x5510, 0x5511, 0x5514, 0x5515, 0x5540, 0x5541, 0x5544, 0x5545,
    0x5550, 0x5551, 0x5554, 0x5555};

void IRAM_ATTR calc_epd_input_1bpp(uint8_t *line_data, uint8_t *epd_input,
                                   enum DrawMode mode) {

  uint32_t *wide_epd_input = (uint32_t *)epd_input;

  // this is reversed for little-endian, but this is later compensated
  // through the output peripheral.
  for (uint32_t j = 0; j < EPD_WIDTH / 16; j++) {
    uint8_t v1 = *(line_data++);
    uint8_t v2 = *(line_data++);
    wide_epd_input[j] = (lut_1bpp[v1] << 16) | lut_1bpp[v2];
  }
}

/*
static inline void calc_lut_pos(
    uint8_t *lut,
    uint32_t pos,
    uint32_t add_mask,
    uint32_t shift_amount
) {
  uint8_t r = k + 1;
  uint32_t add_mask = (r << 24) | (r << 16) | (r << 8) | r;
  uint8_t shift_amount;
  const uint32_t shiftmul = (1 << 15) + (1 << 21) + (1 << 3) + (1 << 9);
  uint32_t val = pos;
  val = (val | (val << 8)) & 0x00FF00FF;
  val = (val | (val << 4)) & 0x0F0F0F0F;
  val += add_mask;
  val = ~val;
  // now the bits we need are masked
  val &= 0x10101010;
  // shift relevant bits to the most significant byte, then shift down
  lut[pos] = ((val * shiftmul) >> shift_amount);
}*/

static void IRAM_ATTR reset_lut(uint8_t *lut_mem, enum DrawMode mode) {
  switch (mode) {
  case BLACK_ON_WHITE:
    memset(lut_mem, 0x55, (1 << 16));
    break;
  case WHITE_ON_BLACK:
  case WHITE_ON_WHITE:
    memset(lut_mem, 0xAA, (1 << 16));
    break;
  default:
    ESP_LOGW("epd_driver", "unknown draw mode %d!", mode);
    break;
  }
}

static void IRAM_ATTR update_LUT(uint8_t *lut_mem, uint8_t k,
                                 enum DrawMode mode) {
  if (mode == BLACK_ON_WHITE || mode == WHITE_ON_WHITE) {
    k = 15 - k;
  }

  // reset the pixels which are not to be lightened / darkened
  // any longer in the current frame
  for (uint32_t l = k; l < (1 << 16); l += 16) {
    lut_mem[l] &= 0xFC;
  }

  for (uint32_t l = (k << 4); l < (1 << 16); l += (1 << 8)) {
    for (uint32_t p = 0; p < 16; p++) {
      lut_mem[l + p] &= 0xF3;
    }
  }
  for (uint32_t l = (k << 8); l < (1 << 16); l += (1 << 12)) {
    for (uint32_t p = 0; p < (1 << 8); p++) {
      lut_mem[l + p] &= 0xCF;
    }
  }
  for (uint32_t p = (k << 12); p < ((k + 1) << 12); p++) {
    lut_mem[p] &= 0x3F;
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

/*
 * bit-shift a buffer `shift` <= 7 bits to the right.
 */
void IRAM_ATTR bit_shift_buffer_right(uint8_t *buf, uint32_t len, int shift) {
  uint8_t carry = 0x00;
  uint8_t mask = 0;
  for (uint32_t i = 0; i < len; i++) {
    uint8_t val = buf[i];
    buf[i] = (val << shift) | carry;
    carry = val >> (8 - shift);
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

typedef struct {
  uint8_t *data_ptr;
  SemaphoreHandle_t done_smphr;
  Rect_t area;
  int frame;
  enum DrawMode mode;
} OutputParams;

void IRAM_ATTR provide_out(OutputParams *params) {
  uint8_t line[EPD_WIDTH / 2];
  memset(line, 255, EPD_WIDTH / 2);
  Rect_t area = params->area;
  uint8_t *ptr = params->data_ptr;

  if (params->frame == 0) {
    reset_lut(conversion_lut, params->mode);
  }

  update_LUT(conversion_lut, params->frame, params->mode);

  if (area.x < 0) {
    ptr += -area.x / 2;
  }
  if (area.y < 0) {
    ptr += (area.width / 2 + area.width % 2) * -area.y;
  }

  for (int i = 0; i < EPD_HEIGHT; i++) {
    if (i < area.y || i >= area.y + area.height) {
      continue;
    }

    uint32_t *lp;
    bool shifted = false;
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
      line_bytes =
          min(line_bytes, EPD_WIDTH / 2 - (uint32_t)(buf_start - line));
      memcpy(buf_start, ptr, line_bytes);
      ptr += area.width / 2 + area.width % 2;

      // mask last nibble for uneven width
      if (area.width % 2 == 1 && area.x / 2 + area.width / 2 + 1 < EPD_WIDTH) {
        *(buf_start + line_bytes - 1) |= 0xF0;
      }
      if (area.x % 2 == 1 && area.x < EPD_WIDTH) {
        shifted = true;
        // shift one nibble to right
        nibble_shift_buffer_right(
            buf_start, min(line_bytes + 1, (uint32_t)line + EPD_WIDTH / 2 -
                                               (uint32_t)buf_start));
      }
      lp = (uint32_t *)line;
    }
    xQueueSendToBack(output_queue, lp, portMAX_DELAY);
    if (shifted) {
      memset(line, 255, EPD_WIDTH / 2);
    }
  }

  xSemaphoreGive(params->done_smphr);
  vTaskDelay(portMAX_DELAY);
}

void IRAM_ATTR feed_display(OutputParams *params) {
  Rect_t area = params->area;
  const int *contrast_lut = contrast_cycles_4;
  switch (params->mode) {
  case WHITE_ON_WHITE:
  case BLACK_ON_WHITE:
    contrast_lut = contrast_cycles_4;
    break;
  case WHITE_ON_BLACK:
    contrast_lut = contrast_cycles_4_white;
    break;
  }

  epd_start_frame();
  for (int i = 0; i < EPD_HEIGHT; i++) {
    if (i < area.y || i >= area.y + area.height) {
      skip_row(contrast_lut[params->frame]);
      continue;
    }
    uint8_t output[EPD_WIDTH / 2];
    xQueueReceive(output_queue, output, portMAX_DELAY);
    calc_epd_input_4bpp((uint32_t *)output, epd_get_current_buffer(),
                        params->frame, conversion_lut);
    write_row(contrast_lut[params->frame]);
  }
  if (!skipping) {
    // Since we "pipeline" row output, we still have to latch out the last row.
    write_row(contrast_lut[params->frame]);
  }
  epd_end_frame();

  xSemaphoreGive(params->done_smphr);
  vTaskDelay(portMAX_DELAY);
}

void IRAM_ATTR epd_draw_frame_1bit(Rect_t area, uint8_t *ptr,
                                   enum DrawMode mode, int time) {

  epd_start_frame();
  uint8_t line[EPD_WIDTH / 8];
  memset(line, 0, sizeof(line));

  if (area.x < 0) {
    ptr += -area.x / 8;
  }

  int ceil_byte_width = (area.width / 8 + (area.width % 8 > 0));
  if (area.y < 0) {
    ptr += ceil_byte_width * -area.y;
  }

  for (int i = 0; i < EPD_HEIGHT; i++) {
    if (i < area.y || i >= area.y + area.height) {
      skip_row(time);
      continue;
    }

    uint8_t *lp;
    bool shifted = 0;
    if (area.width == EPD_WIDTH && area.x == 0) {
      lp = ptr;
      ptr += EPD_WIDTH / 8;
    } else {
      uint8_t *buf_start = (uint8_t *)line;
      uint32_t line_bytes = ceil_byte_width;
      if (area.x >= 0) {
        buf_start += area.x / 8;
      } else {
        // reduce line_bytes to actually used bytes
        line_bytes += area.x / 8;
      }
      line_bytes =
          min(line_bytes, EPD_WIDTH / 8 - (uint32_t)(buf_start - line));
      memcpy(buf_start, ptr, line_bytes);
      ptr += ceil_byte_width;

      // mask last n bits if width is not divisible by 8
      if (area.width % 8 != 0 && ceil_byte_width + 1 < EPD_WIDTH) {
        uint8_t mask = 0;
        for (int s = 0; s < area.width % 8; s++) {
          mask = (mask << 1) | 1;
        }
        *(buf_start + line_bytes - 1) &= mask;
      }

      if (area.x % 8 != 0 && area.x < EPD_WIDTH) {
        // shift to right
        shifted = true;
        bit_shift_buffer_right(
            buf_start,
            min(line_bytes + 1,
                (uint32_t)line + EPD_WIDTH / 8 - (uint32_t)buf_start),
            area.x % 8);
      }
      lp = line;
    }
    calc_epd_input_1bpp(lp, epd_get_current_buffer(), mode);
    write_row(time);
    if (shifted) {
      memset(line, 0, sizeof(line));
    }
  }
  if (!skipping) {
    write_row(time);
  }
  epd_end_frame();
}

void IRAM_ATTR epd_draw_image(Rect_t area, uint8_t *data, enum DrawMode mode) {
  uint8_t line[EPD_WIDTH / 2];
  memset(line, 255, EPD_WIDTH / 2);
  uint8_t frame_count = 15;

  SemaphoreHandle_t fetch_sem = xSemaphoreCreateBinary();
  SemaphoreHandle_t feed_sem = xSemaphoreCreateBinary();
  vTaskDelay(10);
  for (uint8_t k = 0; k < frame_count; k++) {
    OutputParams p1 = {
        .area = area,
        .data_ptr = data,
        .frame = k,
        .mode = mode,
        .done_smphr = fetch_sem,
    };
    OutputParams p2 = {
        .area = area,
        .data_ptr = data,
        .frame = k,
        .mode = mode,
        .done_smphr = feed_sem,
    };

    TaskHandle_t t1, t2;
    xTaskCreatePinnedToCore((void (*)(void *))provide_out, "privide_out", 8000,
                            &p1, 10, &t1, 0);
    xTaskCreatePinnedToCore((void (*)(void *))feed_display, "render", 8000, &p2,
                            10, &t2, 1);

    xSemaphoreTake(fetch_sem, portMAX_DELAY);
    xSemaphoreTake(feed_sem, portMAX_DELAY);

    vTaskDelete(t1);
    vTaskDelete(t2);
  }
  vSemaphoreDelete(fetch_sem);
  vSemaphoreDelete(feed_sem);
}
