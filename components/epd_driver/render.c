#include "epd_driver.h"
#include "epd_temperature.h"
#include "ed097oc4.h"

#include "driver/rtc_io.h"
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "xtensa/core-macros.h"
#include <string.h>
#include "ED097TC2.h"


// FIXME: Waveform interface
#if defined(CONFIG_EPD_DISPLAY_TYPE_ED097OC4) ||                               \
    defined(CONFIG_EPD_DISPLAY_TYPE_ED060SC4) ||                               \
    defined(CONFIG_EPD_DISPLAY_TYPE_ED097OC4_LQ) ||                            \
    defined(CONFIG_EPD_DISPLAY_TYPE_ED047TC1)

/* 4bpp Contrast cycles in order of contrast (Darkest first).  */
const int contrast_cycles_4[15] = {30, 30, 20, 20, 30,  30,  30, 40,
                                   40, 50, 50, 50, 100, 200, 300};

const int contrast_cycles_4_white[15] = {10, 10, 8,  8,  8,  8,   8,  10,
                                         10, 15, 15, 20, 20, 100, 300};
const int clear_cycle_time = 12;

#elif defined(CONFIG_EPD_DISPLAY_TYPE_ED097TC2)
const int contrast_cycles_4[15] = {15, 8,  8,  8,  8,  8,   10, 10,
                                   10, 10, 20, 20, 50, 100, 200};

const int contrast_cycles_4_white[15] = {7, 8, 8, 6, 6, 6,  6,  6,
                                         6, 6, 6, 8, 8, 50, 150};

const int clear_cycle_time = 12;

#elif defined(CONFIG_EPD_DISPLAY_TYPE_ED133UT2)
const int contrast_cycles_4[15] = {60, 60,  40,  40,  60,  60,  60, 80,
                                   80, 100, 100, 100, 200, 200, 300};

const int contrast_cycles_4_white[15] = {50, 30, 30, 30, 30, 30,  30, 30,
                                         30, 30, 50, 50, 50, 100, 200};

const int clear_cycle_time = 12;

#else
#error "no display type defined!"
#endif



// number of bytes needed for one line of EPD pixel data.
#define EPD_LINE_BYTES EPD_WIDTH / 4

#define RTOS_ERROR_CHECK(x)                                                    \
  do {                                                                         \
    esp_err_t __err_rc = (x);                                                  \
    if (__err_rc != pdPASS) {                                                  \
      abort();                                                                 \
    }                                                                          \
  } while (0)

// status tracker for row skipping
uint32_t skipping;

#define CLEAR_BYTE 0B10101010
#define DARK_BYTE 0B01010101

// Heap space to use for the EPD output lookup table, which
// is calculated for each cycle.
static uint8_t *conversion_lut;

// Queue of input data lines
static QueueHandle_t output_queue;

typedef struct {
  const uint8_t *data_ptr;
  SemaphoreHandle_t done_smphr;
  SemaphoreHandle_t start_smphr;
  Rect_t area;
  int frame;
  /// waveform mode when using vendor waveforms
  int waveform_mode;
  /// waveform range when using vendor waveforms
  int waveform_range;
  enum DrawMode mode;
  enum DrawError error;
  const bool *drawn_lines;
} OutputParams;

static OutputParams fetch_params;
static OutputParams feed_params;

/* Python script for generating the 1bpp lookup table:
 * for i in range(256):
     number = 0;
     for b in range(8):
         if not (i & (0x80 >> b)):
             number |= 1 << (2*b)
     print ('0x%04x,'%number)
 */
const static uint32_t lut_1bpp_black[256] = {
    0x5555, 0x1555, 0x4555, 0x0555, 0x5155, 0x1155, 0x4155, 0x0155, 0x5455,
    0x1455, 0x4455, 0x0455, 0x5055, 0x1055, 0x4055, 0x0055, 0x5515, 0x1515,
    0x4515, 0x0515, 0x5115, 0x1115, 0x4115, 0x0115, 0x5415, 0x1415, 0x4415,
    0x0415, 0x5015, 0x1015, 0x4015, 0x0015, 0x5545, 0x1545, 0x4545, 0x0545,
    0x5145, 0x1145, 0x4145, 0x0145, 0x5445, 0x1445, 0x4445, 0x0445, 0x5045,
    0x1045, 0x4045, 0x0045, 0x5505, 0x1505, 0x4505, 0x0505, 0x5105, 0x1105,
    0x4105, 0x0105, 0x5405, 0x1405, 0x4405, 0x0405, 0x5005, 0x1005, 0x4005,
    0x0005, 0x5551, 0x1551, 0x4551, 0x0551, 0x5151, 0x1151, 0x4151, 0x0151,
    0x5451, 0x1451, 0x4451, 0x0451, 0x5051, 0x1051, 0x4051, 0x0051, 0x5511,
    0x1511, 0x4511, 0x0511, 0x5111, 0x1111, 0x4111, 0x0111, 0x5411, 0x1411,
    0x4411, 0x0411, 0x5011, 0x1011, 0x4011, 0x0011, 0x5541, 0x1541, 0x4541,
    0x0541, 0x5141, 0x1141, 0x4141, 0x0141, 0x5441, 0x1441, 0x4441, 0x0441,
    0x5041, 0x1041, 0x4041, 0x0041, 0x5501, 0x1501, 0x4501, 0x0501, 0x5101,
    0x1101, 0x4101, 0x0101, 0x5401, 0x1401, 0x4401, 0x0401, 0x5001, 0x1001,
    0x4001, 0x0001, 0x5554, 0x1554, 0x4554, 0x0554, 0x5154, 0x1154, 0x4154,
    0x0154, 0x5454, 0x1454, 0x4454, 0x0454, 0x5054, 0x1054, 0x4054, 0x0054,
    0x5514, 0x1514, 0x4514, 0x0514, 0x5114, 0x1114, 0x4114, 0x0114, 0x5414,
    0x1414, 0x4414, 0x0414, 0x5014, 0x1014, 0x4014, 0x0014, 0x5544, 0x1544,
    0x4544, 0x0544, 0x5144, 0x1144, 0x4144, 0x0144, 0x5444, 0x1444, 0x4444,
    0x0444, 0x5044, 0x1044, 0x4044, 0x0044, 0x5504, 0x1504, 0x4504, 0x0504,
    0x5104, 0x1104, 0x4104, 0x0104, 0x5404, 0x1404, 0x4404, 0x0404, 0x5004,
    0x1004, 0x4004, 0x0004, 0x5550, 0x1550, 0x4550, 0x0550, 0x5150, 0x1150,
    0x4150, 0x0150, 0x5450, 0x1450, 0x4450, 0x0450, 0x5050, 0x1050, 0x4050,
    0x0050, 0x5510, 0x1510, 0x4510, 0x0510, 0x5110, 0x1110, 0x4110, 0x0110,
    0x5410, 0x1410, 0x4410, 0x0410, 0x5010, 0x1010, 0x4010, 0x0010, 0x5540,
    0x1540, 0x4540, 0x0540, 0x5140, 0x1140, 0x4140, 0x0140, 0x5440, 0x1440,
    0x4440, 0x0440, 0x5040, 0x1040, 0x4040, 0x0040, 0x5500, 0x1500, 0x4500,
    0x0500, 0x5100, 0x1100, 0x4100, 0x0100, 0x5400, 0x1400, 0x4400, 0x0400,
    0x5000, 0x1000, 0x4000, 0x0000};

inline uint32_t min(uint32_t x, uint32_t y) { return x < y ? x : y; }
inline uint32_t max(uint32_t x, uint32_t y) { return x > y ? x : y; }

// output a row to the display.
static void IRAM_ATTR write_row(uint32_t output_time_dus) {
  skipping = 0;
  epd_output_row(output_time_dus);
}

// skip a display row
void IRAM_ATTR skip_row(uint8_t pipeline_finish_time) {
  // output previously loaded row, fill buffer with no-ops.
  if (skipping < 2) {
    memset(epd_get_current_buffer(), 0, EPD_LINE_BYTES);
    epd_output_row(pipeline_finish_time);
  } else {
    epd_skip();
  }
  skipping++;
}


/*
 * Reorder the output buffer to account for I2S FIFO order.
 */
static void IRAM_ATTR reorder_line_buffer(uint32_t *line_data) {
  for (uint32_t i = 0; i < EPD_LINE_BYTES / 4; i++) {
    uint32_t val = *line_data;
    *(line_data++) = val >> 16 | ((val & 0x0000FFFF) << 16);
  }
}

/**
 * bit-shift a buffer `shift` <= 7 bits to the right.
 */
static void IRAM_ATTR bit_shift_buffer_right(uint8_t *buf, uint32_t len, int shift) {
  uint8_t carry = 0xFF << (8 - shift);
  for (uint32_t i = 0; i < len; i++) {
    uint8_t val = buf[i];
    buf[i] = (val >> shift) | carry;
    carry = val << (8 - shift);
  }
}

static void IRAM_ATTR nibble_shift_buffer_right(uint8_t *buf, uint32_t len) {
  uint8_t carry = 0xF;
  for (uint32_t i = 0; i < len; i++) {
    uint8_t val = buf[i];
    buf[i] = (val << 4) | carry;
    carry = (val & 0xF0) >> 4;
  }
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

///////////////////////////// Looking up EPD Pixels ///////////////////////////////

static void IRAM_ATTR calc_epd_input_1bpp(const uint32_t *line_data,
                                   uint8_t *epd_input, const uint8_t *lut) {

  uint32_t *wide_epd_input = (uint32_t *)epd_input;
  uint8_t *data_ptr = (uint8_t *)line_data;
  uint32_t *lut_32 = (uint32_t *)lut;
  // this is reversed for little-endian, but this is later compensated
  // through the output peripheral.
  for (uint32_t j = 0; j < EPD_WIDTH / 16; j++) {
    uint8_t v1 = *(data_ptr++);
    uint8_t v2 = *(data_ptr++);
    wide_epd_input[j] = (lut_32[v1] << 16) | lut_32[v2];
  }
}

static void IRAM_ATTR calc_epd_input_4bpp(const uint32_t *line_data,
                                   uint8_t *epd_input,
                                   const uint8_t *conversion_lut) {

  uint32_t *wide_epd_input = (uint32_t *)epd_input;
  const uint16_t *line_data_16 = (const uint16_t *)line_data;

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

/**
 * Look up 4 pixels of a differential image.
 */
static inline uint8_t lookup_differential_pixels(uint32_t in,
                                      const uint8_t *conversion_lut) {
  uint8_t out = (conversion_lut + 0x100)[in & 0xFF];
  in = in >> 8;
  out |= conversion_lut[in & 0xFF];
  in = in >> 8;
  out |= (conversion_lut + 0x300)[in & 0xFF];
  in = in >> 8;
  out |= (conversion_lut + 0x200)[in];
  return out;
}

/**
 * Calculate EPD input for a difference image with one pixel per byte.
 */
static void IRAM_ATTR calc_epd_input_1ppB(const uint32_t *ld, uint8_t *epd_input,
                                   const uint8_t *conversion_lut) {

  // this is reversed for little-endian, but this is later compensated
  // through the output peripheral.
  for (uint32_t j = 0; j < EPD_WIDTH / 4; j += 4) {
    epd_input[j + 2] = lookup_differential_pixels(*(ld++), conversion_lut);
    epd_input[j + 3] = lookup_differential_pixels(*(ld++), conversion_lut);
    epd_input[j + 0] = lookup_differential_pixels(*(ld++), conversion_lut);
    epd_input[j + 1] = lookup_differential_pixels(*(ld++), conversion_lut);
  }
}


///////////////////////////// Calculate Lookup Tables ///////////////////////////////

static void IRAM_ATTR update_epdiy_lut(uint8_t *lut_mem, uint8_t k,
                                 enum DrawMode mode) {
  /*
  if (mode == BLACK_ON_WHITE || mode == WHITE_ON_WHITE) {
    k = 15 - k;
  }
  */

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

/**
 * Unpack the waveform data into a lookup table, with bit shifted copies.
 */
static void IRAM_ATTR waveform_lut(uint8_t *lut, uint8_t mode, int range, int frame) {
  const uint8_t *p_lut =
      waveform_info.mode_data[mode]->range_data[range]->luts + (16 * 4 * frame);
  for (uint8_t to = 0; to < 16; to++) {
    for (uint8_t from_packed = 0; from_packed < 4; from_packed++) {
      uint8_t index = (to << 4) | (from_packed * 4);
      uint8_t packed = *(p_lut++);
      lut[index] = (packed >> 6) & 3;
      lut[index + 1] = (packed >> 4) & 3;
      lut[index + 2] = (packed >> 2) & 3;
      lut[index + 3] = (packed >> 0) & 3;
      // printf("%2X%2X%2X%2X (%d)", lut[index], lut[index + 1], lut[index + 2],
      // lut[index + 3], index);
    }
    // printf("\n");
  }
  uint32_t index = 0x100;
  for (uint8_t s = 2; s <= 6; s += 2) {
    for (int i = 0; i < 0x100; i++) {
      lut[index] = lut[index % 0x100] << s;
      index++;
    }
  }
}

/**
 * Build a 16-bit LUT from the waveform if the previous color is
 * known, e.g. all white or all black.
 * This LUT is use to look up 4 pixels at once, as with the epdiy LUT.
 */
static void IRAM_ATTR waveform_lut_static_from(uint8_t *lut, uint8_t from,
                                        uint8_t mode, int range, int frame) {
  const uint8_t *p_lut =
      waveform_info.mode_data[mode]->range_data[range]->luts + (16 * 4 * frame);

  /// index into the packed "from" row
  uint8_t fi = from >> 2;
  /// bit shift amount for the packed "from" row
  uint8_t fs = 2 * (from & 3);

  // FIXME: Optimize this
  for (uint8_t t1 = 0; t1 < 16; t1++) {
    uint8_t v1 = (p_lut[(t1 << 2) + fi] >> fs) << 6;
    uint32_t s1 = t1 << 12;
    for (uint8_t t2 = 0; t2 < 16; t2++) {
      uint8_t v2 = (p_lut[(t2 << 2) + fi] >> fs) << 4;
      uint32_t s2 = t2 << 8;
      for (uint8_t t3 = 0; t3 < 16; t3++) {
        uint8_t v3 = (p_lut[(t3 << 2) + fi] >> fs) << 2;
        uint32_t s3 = t3 << 4;
        for (uint8_t t4 = 0; t4 < 16; t4++) {
          uint8_t v4 = (p_lut[(t4 << 2) + fi] >> fs) << 0;
          uint32_t s4 = t4;
          lut[s1 | s2 | s3 | s4] = v1 | v2 | v3 | v4;
        }
      }
    }
  }
}


///////////////////////////// Coordination ///////////////////////////////


static void IRAM_ATTR provide_out(OutputParams *params) {
  while (true) {
    // line must be able to hold 2-pixel-per-byte or 1-pixel-per-byte data
    uint8_t line[EPD_WIDTH];
    memset(line, 255, EPD_WIDTH);

    xSemaphoreTake(params->start_smphr, portMAX_DELAY);
    Rect_t area = params->area;
    const uint8_t *ptr = params->data_ptr;

    // number of pixels per byte of input data
    int ppB = 0;
    int bytes_per_line = 0;
    int width_divider = 0;

    if (params->mode & MODE_PACKING_1PPB_DIFFERENCE) {
      ppB = 1;
      bytes_per_line = area.width;
      width_divider = 1;
    } else if (params->mode & MODE_PACKING_2PPB) {
      ppB = 2;
      bytes_per_line = area.width / 2 + area.width % 2;
      width_divider = 2;
    } else if (params->mode & MODE_PACKING_8PPB) {
      ppB = 8;
      bytes_per_line = (area.width / 8 + (area.width % 8 > 0));
      width_divider = 8;
    } else {
      params->error |= DRAW_INVALID_PACKING_MODE;
    }

    if (area.x < 0) {
      ptr += -area.x / width_divider;
    }
    if (area.y < 0) {
      ptr += bytes_per_line * -area.y;
    }

    for (int i = 0; i < EPD_HEIGHT; i++) {
      if (i < area.y || i >= area.y + area.height) {
        continue;
      }
      if (params->drawn_lines != NULL && !params->drawn_lines[i - area.y]) {
        ptr += bytes_per_line;
        continue;
      }

      uint32_t *lp = (uint32_t *)line;
      bool shifted = false;
      if (area.width == EPD_WIDTH && area.x == 0 && !params->error) {
        lp = (uint32_t *)ptr;
        ptr += bytes_per_line;
      } else if (!params->error) {
        uint8_t *buf_start = (uint8_t *)line;
        uint32_t line_bytes = bytes_per_line;
        if (area.x >= 0) {
          buf_start += area.x / width_divider;
        } else {
          // reduce line_bytes to actually used bytes
          line_bytes += area.x / width_divider;
        }
        line_bytes = min(line_bytes, EPD_WIDTH / width_divider -
                                         (uint32_t)(buf_start - line));
        memcpy(buf_start, ptr, line_bytes);
        ptr += bytes_per_line;

        /// consider half-byte shifts in two-pixel-per-Byte mode.
        if (ppB == 2) {
          // mask last nibble for uneven width
          if (area.width % 2 == 1 &&
              area.x / 2 + area.width / 2 + 1 < EPD_WIDTH) {
            *(buf_start + line_bytes - 1) |= 0xF0;
          }
          if (area.x % 2 == 1 && area.x < EPD_WIDTH) {
            shifted = true;
            uint32_t remaining =
                (uint32_t)line + EPD_WIDTH / 2 - (uint32_t)buf_start;
            uint32_t to_shift = min(line_bytes + 1, remaining);
            // shift one nibble to right
            nibble_shift_buffer_right(buf_start, to_shift);
          }
          // consider bit shifts in bit buffers
        } else if (ppB == 8) {
          // mask last n bits if width is not divisible by 8
          if (area.width % 8 != 0 && bytes_per_line + 1 < EPD_WIDTH) {
            uint8_t mask = 0;
            for (int s = 0; s < area.width % 8; s++) {
              mask = (mask << 1) | 1;
            }
            *(buf_start + line_bytes - 1) |= ~mask;
          }

          if (area.x % 8 != 0 && area.x < EPD_WIDTH) {
            // shift to right
            shifted = true;
            uint32_t remaining =
                (uint32_t)line + EPD_WIDTH / 8 - (uint32_t)buf_start;
            uint32_t to_shift = min(line_bytes + 1, remaining);
            bit_shift_buffer_right(buf_start, to_shift, area.x % 8);
          }
        }
        lp = (uint32_t *)line;
      }
      xQueueSendToBack(output_queue, lp, portMAX_DELAY);
      if (shifted) {
        memset(line, 255, EPD_WIDTH / width_divider);
      }
    }

    xSemaphoreGive(params->done_smphr);
  }
}

static void IRAM_ATTR feed_display(OutputParams *params) {
  while (true) {
    xSemaphoreTake(params->start_smphr, portMAX_DELAY);

    Rect_t area = params->area;
    const int *contrast_lut = contrast_cycles_4;
    enum DrawMode mode = params->mode;
    int frame_time = 10;

    // use approximated waveforms
    if (mode & EPDIY_WAVEFORM) {
      frame_time = contrast_lut[params->frame];
      if (mode & MODE_PACKING_2PPB) {
        if (params->frame == 0) {
          memset(conversion_lut, 0x55, (1 << 16));
        }

        update_epdiy_lut(conversion_lut, params->frame, mode);
      } else if (mode & MODE_PACKING_8PPB) {
        memcpy(conversion_lut, lut_1bpp_black, sizeof(lut_1bpp_black));
      } else {
        params->error |= DRAW_LOOKUP_NOT_IMPLEMENTED;
      }

      // use vendor waveforms
    } else if (mode & VENDOR_WAVEFORM) {
      if (mode & MODE_PACKING_2PPB && mode & PREVIOUSLY_WHITE) {
        waveform_lut_static_from(conversion_lut, 0x0F, params->waveform_mode,
                                 params->waveform_range, params->frame);
      } else if (mode & MODE_PACKING_2PPB && mode & PREVIOUSLY_BLACK) {
        waveform_lut_static_from(conversion_lut, 0x00, params->waveform_mode,
                                 params->waveform_range, params->frame);
      } else if (mode & MODE_PACKING_1PPB_DIFFERENCE) {
        waveform_lut(conversion_lut, params->waveform_mode,
                     params->waveform_range, params->frame);
      } else {
        params->error |= DRAW_LOOKUP_NOT_IMPLEMENTED;
      }
    }

    void (*input_calc_func)(const uint32_t *, uint8_t *, const uint8_t *) =
        NULL;
    if (mode & MODE_PACKING_2PPB) {
      input_calc_func = &calc_epd_input_4bpp;
    } else if (mode & (MODE_PACKING_1PPB_DIFFERENCE | VENDOR_WAVEFORM)) {
      input_calc_func = &calc_epd_input_1ppB;
    } else if (mode & (MODE_PACKING_8PPB | EPDIY_WAVEFORM)) {
      input_calc_func = &calc_epd_input_1bpp;
    } else {
      params->error |= DRAW_LOOKUP_NOT_IMPLEMENTED;
    }

    epd_start_frame();
    for (int i = 0; i < EPD_HEIGHT; i++) {
      if (i < area.y || i >= area.y + area.height) {
        skip_row(frame_time);
        continue;
      }
      if (params->drawn_lines != NULL && !params->drawn_lines[i - area.y]) {
        skip_row(frame_time);
        continue;
      }
      uint8_t output[EPD_WIDTH];
      xQueueReceive(output_queue, output, portMAX_DELAY);
      if (!params->error) {
        (*input_calc_func)((uint32_t *)output, epd_get_current_buffer(),
                           conversion_lut);
      }
      write_row(frame_time);
    }
    if (!skipping) {
      // Since we "pipeline" row output, we still have to latch out the last
      // row.
      write_row(frame_time);
    }
    epd_end_frame();

    xSemaphoreGive(params->done_smphr);
  }
}


////////////////////////////////  API Procedures //////////////////////////////////

void IRAM_ATTR epd_draw_image_lines(Rect_t area, const uint8_t *data,
                                    enum DrawMode mode,
                                    const bool *drawn_lines) {
  uint8_t line[EPD_WIDTH / 2];
  memset(line, 255, EPD_WIDTH / 2);

  int waveform_range = 7;
  int waveform_mode = 0;
  uint8_t frame_count = 0;
  if (mode & EPDIY_WAVEFORM) {
    frame_count = 15;
  } else if (mode & VENDOR_WAVEFORM) {
    waveform_mode = mode & 0x0F;
    frame_count = waveform_info.mode_data[waveform_mode]
                      ->range_data[waveform_range]
                      ->phases;
  }

  for (uint8_t k = 0; k < frame_count; k++) {
    uint64_t frame_start = esp_timer_get_time() / 1000;
    fetch_params.area = area;
    fetch_params.data_ptr = data;
    fetch_params.frame = k;
    fetch_params.waveform_range = waveform_range;
    fetch_params.waveform_mode = waveform_mode;
    fetch_params.mode = mode;
    fetch_params.error = DRAW_SUCCESS;
    fetch_params.drawn_lines = drawn_lines;

    feed_params.area = area;
    feed_params.data_ptr = data;
    feed_params.frame = k;
    feed_params.waveform_range = waveform_range;
    feed_params.waveform_mode = waveform_mode;
    feed_params.mode = mode;
    feed_params.error = DRAW_SUCCESS;
    feed_params.drawn_lines = drawn_lines;

    xSemaphoreGive(fetch_params.start_smphr);
    xSemaphoreGive(feed_params.start_smphr);
    xSemaphoreTake(fetch_params.done_smphr, portMAX_DELAY);
    xSemaphoreTake(feed_params.done_smphr, portMAX_DELAY);

    uint64_t frame_end = esp_timer_get_time() / 1000;
    if (frame_end - frame_start < MINIMUM_FRAME_TIME) {
      vTaskDelay(min(MINIMUM_FRAME_TIME - (frame_end - frame_start),
                     MINIMUM_FRAME_TIME));
    }
  }
}

void epd_clear_area(Rect_t area) {
  epd_clear_area_cycles(area, 3, clear_cycle_time);
}

void epd_clear_area_cycles(Rect_t area, int cycles, int cycle_time) {
  const short white_time = cycle_time;
  const short dark_time = cycle_time;

  for (int c = 0; c < cycles; c++) {
    for (int i = 0; i < 10; i++) {
      epd_push_pixels(area, dark_time, 0);
    }
    for (int i = 0; i < 10; i++) {
      epd_push_pixels(area, white_time, 1);
    }
  }
}



void epd_init() {
  skipping = 0;
  epd_base_init(EPD_WIDTH);
  epd_temperature_init();

  fetch_params.done_smphr = xSemaphoreCreateBinary();
  fetch_params.start_smphr = xSemaphoreCreateBinary();

  feed_params.done_smphr = xSemaphoreCreateBinary();
  feed_params.start_smphr = xSemaphoreCreateBinary();

  RTOS_ERROR_CHECK(xTaskCreatePinnedToCore((void (*)(void *))provide_out,
                                           "epd_fetch", 1 << 12, &fetch_params, 5,
                                           NULL, 0));

  RTOS_ERROR_CHECK(xTaskCreatePinnedToCore((void (*)(void *))feed_display,
                                           "epd_feed", 1 << 12, &feed_params,
                                           5, NULL, 1));

  conversion_lut = (uint8_t *)heap_caps_malloc(1 << 16, MALLOC_CAP_8BIT);
  assert(conversion_lut != NULL);
  output_queue = xQueueCreate(32, EPD_WIDTH);
}

void epd_deinit() {
  // FIXME: deinit processes
#if defined(CONFIG_EPD_BOARD_REVISION_V5)
  gpio_reset_pin(CKH);
  rtc_gpio_isolate(CKH);
#endif
  epd_base_deinit();
}
