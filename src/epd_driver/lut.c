#include "lut.h"
#include "driver/gpio.h"

#ifndef CONFIG_EPD_BOARD_S3_PROTOTYPE
#include "display_ops.h"
#endif

#include "esp_log.h"
#include "freertos/task.h"
#include <string.h>

#include "esp_system.h" // for ESP_IDF_VERSION_VAL
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "esp_timer.h"
#endif


/*
 * Build Lookup tables and translate via LUTs.
 * WARNING: These functions must only ever write to internal memory,
 *          Since we disable the PSRAM workaround here for performance reasons.
 */

/* Python script for generating the 1bpp lookup table:
 * for i in range(256):
     number = 0;
     for b in range(8):
         if not (i & (b << 1)):
             number |= 1 << (2*b)
     print ('0x%04x,'%number)
 */
const uint32_t lut_1bpp_black[256] = {
    0x5555, 0x5554, 0x5551, 0x5550, 0x5545, 0x5544, 0x5541, 0x5540, 0x5515,
    0x5514, 0x5511, 0x5510, 0x5505, 0x5504, 0x5501, 0x5500, 0x5455, 0x5454,
    0x5451, 0x5450, 0x5445, 0x5444, 0x5441, 0x5440, 0x5415, 0x5414, 0x5411,
    0x5410, 0x5405, 0x5404, 0x5401, 0x5400, 0x5155, 0x5154, 0x5151, 0x5150,
    0x5145, 0x5144, 0x5141, 0x5140, 0x5115, 0x5114, 0x5111, 0x5110, 0x5105,
    0x5104, 0x5101, 0x5100, 0x5055, 0x5054, 0x5051, 0x5050, 0x5045, 0x5044,
    0x5041, 0x5040, 0x5015, 0x5014, 0x5011, 0x5010, 0x5005, 0x5004, 0x5001,
    0x5000, 0x4555, 0x4554, 0x4551, 0x4550, 0x4545, 0x4544, 0x4541, 0x4540,
    0x4515, 0x4514, 0x4511, 0x4510, 0x4505, 0x4504, 0x4501, 0x4500, 0x4455,
    0x4454, 0x4451, 0x4450, 0x4445, 0x4444, 0x4441, 0x4440, 0x4415, 0x4414,
    0x4411, 0x4410, 0x4405, 0x4404, 0x4401, 0x4400, 0x4155, 0x4154, 0x4151,
    0x4150, 0x4145, 0x4144, 0x4141, 0x4140, 0x4115, 0x4114, 0x4111, 0x4110,
    0x4105, 0x4104, 0x4101, 0x4100, 0x4055, 0x4054, 0x4051, 0x4050, 0x4045,
    0x4044, 0x4041, 0x4040, 0x4015, 0x4014, 0x4011, 0x4010, 0x4005, 0x4004,
    0x4001, 0x4000, 0x1555, 0x1554, 0x1551, 0x1550, 0x1545, 0x1544, 0x1541,
    0x1540, 0x1515, 0x1514, 0x1511, 0x1510, 0x1505, 0x1504, 0x1501, 0x1500,
    0x1455, 0x1454, 0x1451, 0x1450, 0x1445, 0x1444, 0x1441, 0x1440, 0x1415,
    0x1414, 0x1411, 0x1410, 0x1405, 0x1404, 0x1401, 0x1400, 0x1155, 0x1154,
    0x1151, 0x1150, 0x1145, 0x1144, 0x1141, 0x1140, 0x1115, 0x1114, 0x1111,
    0x1110, 0x1105, 0x1104, 0x1101, 0x1100, 0x1055, 0x1054, 0x1051, 0x1050,
    0x1045, 0x1044, 0x1041, 0x1040, 0x1015, 0x1014, 0x1011, 0x1010, 0x1005,
    0x1004, 0x1001, 0x1000, 0x0555, 0x0554, 0x0551, 0x0550, 0x0545, 0x0544,
    0x0541, 0x0540, 0x0515, 0x0514, 0x0511, 0x0510, 0x0505, 0x0504, 0x0501,
    0x0500, 0x0455, 0x0454, 0x0451, 0x0450, 0x0445, 0x0444, 0x0441, 0x0440,
    0x0415, 0x0414, 0x0411, 0x0410, 0x0405, 0x0404, 0x0401, 0x0400, 0x0155,
    0x0154, 0x0151, 0x0150, 0x0145, 0x0144, 0x0141, 0x0140, 0x0115, 0x0114,
    0x0111, 0x0110, 0x0105, 0x0104, 0x0101, 0x0100, 0x0055, 0x0054, 0x0051,
    0x0050, 0x0045, 0x0044, 0x0041, 0x0040, 0x0015, 0x0014, 0x0011, 0x0010,
    0x0005, 0x0004, 0x0001, 0x0000};

// Timestamp when the last frame draw was started.
// This is used to enforce a minimum frame draw time, allowing
// all pixels to set.
static uint64_t last_frame_start = 0;

inline int min(int x, int y) { return x < y ? x : y; }
inline int max(int x, int y) { return x > y ? x : y; }

// status tracker for row skipping
uint32_t skipping;


__attribute__((optimize("O3")))
void IRAM_ATTR reorder_line_buffer(uint32_t *line_data) {
  for (uint32_t i = 0; i < EPD_LINE_BYTES / 4; i++) {
    uint32_t val = *line_data;
    *(line_data++) = val >> 16 | ((val & 0x0000FFFF) << 16);
  }
}

__attribute__((optimize("O3")))
void IRAM_ATTR bit_shift_buffer_right(uint8_t *buf, uint32_t len, int shift) {
  uint8_t carry = 0xFF << (8 - shift);
  for (uint32_t i = 0; i < len; i++) {
    uint8_t val = buf[i];
    buf[i] = (val >> shift) | carry;
    carry = val << (8 - shift);
  }
}

__attribute__((optimize("O3")))
void IRAM_ATTR nibble_shift_buffer_right(uint8_t *buf, uint32_t len) {
  uint8_t carry = 0xF;
  for (uint32_t i = 0; i < len; i++) {
    uint8_t val = buf[i];
    buf[i] = (val << 4) | carry;
    carry = (val & 0xF0) >> 4;
  }
}

///////////////////////////// Looking up EPD Pixels
//////////////////////////////////

__attribute__((optimize("O3")))
void IRAM_ATTR calc_epd_input_1bpp(
    const uint32_t *line_data,
    uint8_t *epd_input,
    const uint8_t *lut,
    uint32_t epd_width
) {

  uint32_t *wide_epd_input = (uint32_t *)epd_input;
  uint8_t *data_ptr = (uint8_t *)line_data;
  uint32_t *lut_32 = (uint32_t *)lut;
  // this is reversed for little-endian, but this is later compensated
  // through the output peripheral.
  for (uint32_t j = 0; j < epd_width / 16; j++) {
    uint8_t v1 = *(data_ptr++);
    uint8_t v2 = *(data_ptr++);
    wide_epd_input[j] = (lut_32[v1] << 16) | lut_32[v2];
  }
}

__attribute__((optimize("O3")))
void IRAM_ATTR calc_epd_input_4bpp_lut_64k(
    const uint32_t *line_data,
    uint8_t *epd_input,
    const uint8_t *conversion_lut,
    uint32_t epd_width
) {

  uint32_t *wide_epd_input = (uint32_t *)epd_input;
  const uint16_t *line_data_16 = (const uint16_t *)line_data;

  // this is reversed for little-endian, but this is later compensated
  // through the output peripheral.
  for (uint32_t j = 0; j < epd_width / 16; j++) {

    uint16_t v1 = *(line_data_16++);
    uint16_t v2 = *(line_data_16++);
    uint16_t v3 = *(line_data_16++);
    uint16_t v4 = *(line_data_16++);

#ifndef CONFIG_EPD_BOARD_S3_PROTOTYPE
    uint32_t pixel = conversion_lut[v1] << 16 | conversion_lut[v2] << 24 |
                     conversion_lut[v3] | conversion_lut[v4] << 8;
#else
    uint32_t pixel = conversion_lut[v4];
    pixel = pixel << 8;
    pixel |= conversion_lut[v3];
    pixel = pixel << 8;
    pixel |= conversion_lut[v2];
    pixel = pixel << 8;
    pixel |= conversion_lut[v1];
#endif
    wide_epd_input[j] = pixel;
  }
}

/**
 * Look up 4 pixels of a differential image.
 */
__attribute__((optimize("O3")))
static inline uint8_t lookup_differential_pixels(const uint32_t in, const uint8_t *conversion_lut) {
  uint8_t out = conversion_lut[(in >> 24) & 0xFF];
  out |= (conversion_lut + 0x100)[(in >> 16) & 0xFF];
  out |= (conversion_lut + 0x200)[(in >> 8) & 0xFF];
  out |= (conversion_lut + 0x300)[in & 0xFF];
  return out;
}

/**
 * Calculate EPD input for a difference image with one pixel per byte.
 */
__attribute__((optimize("O3")))
void IRAM_ATTR calc_epd_input_1ppB(
    const uint32_t *ld,
    uint8_t *epd_input,
    const uint8_t *conversion_lut,
    uint32_t epd_width
) {

  // this is reversed for little-endian, but this is later compensated
  // through the output peripheral.
  for (uint32_t j = 0; j < epd_width / 4; j += 4) {
#ifndef CONFIG_EPD_BOARD_S3_PROTOTYPE
    epd_input[j + 2] = lookup_differential_pixels(*(ld++), conversion_lut);
    epd_input[j + 3] = lookup_differential_pixels(*(ld++), conversion_lut);
    epd_input[j + 0] = lookup_differential_pixels(*(ld++), conversion_lut);
    epd_input[j + 1] = lookup_differential_pixels(*(ld++), conversion_lut);
#else
    epd_input[j + 0] = lookup_differential_pixels(*(ld++), conversion_lut);
    epd_input[j + 1] = lookup_differential_pixels(*(ld++), conversion_lut);
    epd_input[j + 2] = lookup_differential_pixels(*(ld++), conversion_lut);
    epd_input[j + 3] = lookup_differential_pixels(*(ld++), conversion_lut);
#endif
  }
}

/**
 * Calculate EPD input for a difference image with one pixel per byte.
 */

__attribute__((optimize("O3")))
void IRAM_ATTR calc_epd_input_1ppB_64k(
    const uint32_t *ld,
    uint8_t *epd_input,
    const uint8_t *conversion_lut,
    uint32_t epd_width
) {

    const uint16_t* lp = (uint16_t*) ld;
#ifdef CONFIG_EPD_BOARD_S3_PROTOTYPE
    for (uint32_t j = 0; j < epd_width / 4; j++) {
      epd_input[j] = (conversion_lut[lp[2 * j + 1]] << 4) | conversion_lut[lp[2 * j]];
    }
#else
  // this is reversed for little-endian, but this is later compensated
  // through the output peripheral.
  for (uint32_t j = 0; j < epd_width / 4; j += 4) {
    epd_input[j + 2] = conversion_lut[*(lp++)];;
    epd_input[j + 2] |=  (conversion_lut[*(lp++)] << 4);
    epd_input[j + 3] = conversion_lut[*(lp++)];;
    epd_input[j + 3] |=  (conversion_lut[*(lp++)] << 4);
    epd_input[j + 0] = conversion_lut[*(lp++)];;
    epd_input[j + 0] |=  (conversion_lut[*(lp++)] << 4);
    epd_input[j + 1] = conversion_lut[*(lp++)];;
    epd_input[j + 1] |=  (conversion_lut[*(lp++)] << 4);
  }
#endif
}


/**
 * Look up 4 pixels in a 1K LUT with fixed "from" value.
 */
__attribute__((optimize("O3")))
uint8_t lookup_pixels_4bpp_1k(
    uint16_t in,
    const uint8_t *conversion_lut,
    uint8_t from,
    uint32_t epd_width
) {
  uint8_t v;
  uint8_t out;

  v = ((in << 4) | from);
  out = conversion_lut[v & 0xFF];
  v = ((in & 0xF0) | from);
  out |= (conversion_lut + 0x100)[v & 0xFF];
  in = in >> 8;
  v = ((in << 4) | from);
  out |= (conversion_lut + 0x200)[v & 0xFF];
  v = ((in & 0xF0) | from);
  out |= (conversion_lut + 0x300)[v];
  return out;
}

/**
 * Calculate EPD input for a 4bpp buffer, but with a difference image LUT.
 * This is used for small-LUT mode.
 */
__attribute__((optimize("O3")))
void IRAM_ATTR calc_epd_input_4bpp_1k_lut(
    const uint32_t *ld,
    uint8_t *epd_input,
    const uint8_t *conversion_lut,
    uint8_t from,
    uint32_t epd_width
) {

  uint16_t *ptr = (uint16_t *)ld;
  // this is reversed for little-endian, but this is later compensated
  // through the output peripheral.
  for (uint32_t j = 0; j < epd_width / 4; j += 4) {
#ifndef CONFIG_EPD_BOARD_S3_PROTOTYPE
    epd_input[j + 2] = lookup_pixels_4bpp_1k(*(ptr++), conversion_lut, from, epd_width);
    epd_input[j + 3] = lookup_pixels_4bpp_1k(*(ptr++), conversion_lut, from, epd_width);
    epd_input[j + 0] = lookup_pixels_4bpp_1k(*(ptr++), conversion_lut, from, epd_width);
    epd_input[j + 1] = lookup_pixels_4bpp_1k(*(ptr++), conversion_lut, from, epd_width);
#else
    epd_input[j + 0] = lookup_pixels_4bpp_1k(*(ptr++), conversion_lut, from, epd_width);
    epd_input[j + 1] = lookup_pixels_4bpp_1k(*(ptr++), conversion_lut, from, epd_width);
    epd_input[j + 2] = lookup_pixels_4bpp_1k(*(ptr++), conversion_lut, from, epd_width);
    epd_input[j + 3] = lookup_pixels_4bpp_1k(*(ptr++), conversion_lut, from, epd_width);
#endif
  }
}

__attribute__((optimize("O3")))
void IRAM_ATTR calc_epd_input_4bpp_1k_lut_white(
    const uint32_t *ld,
    uint8_t *epd_input,
    const uint8_t *conversion_lut,
    uint32_t epd_width
) {
  calc_epd_input_4bpp_1k_lut(ld, epd_input, conversion_lut, 0xF, epd_width);
}

__attribute__((optimize("O3")))
void IRAM_ATTR calc_epd_input_4bpp_1k_lut_black(
    const uint32_t *ld,
    uint8_t *epd_input,
    const uint8_t *conversion_lut,
    uint32_t epd_width
) {
  calc_epd_input_4bpp_1k_lut(ld, epd_input, conversion_lut, 0x0, epd_width);
}

///////////////////////////// Calculate Lookup Tables
//////////////////////////////////

/**
 * Unpack the waveform data into a lookup table, with bit shifted copies.
 */
__attribute__((optimize("O3")))
static void IRAM_ATTR waveform_lut(
    uint8_t *lut,
    const EpdWaveformPhases *phases,
    int frame
) {
  const uint8_t *p_lut = phases->luts + (16 * 4 * frame);
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
 * Unpack the waveform data into a lookup table,
 * 64k to loop up two bytes at once
 */
__attribute__((optimize("O3")))
static void IRAM_ATTR waveform_lut_64k(
    uint8_t *lut,
    const EpdWaveformPhases *phases,
    int frame
) {

  const uint8_t *p_lut = phases->luts + (16 * 4 * frame);
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

  for (int outer = 0xFF; outer >= 0; outer--) {
    uint32_t outer_result = lut[outer] << 2;
    outer_result = (outer_result << 16) | outer_result;
    outer_result = (outer_result << 8) | outer_result;
    uint32_t* lut_section = (uint32_t*)(&lut[outer << 8]);
    memcpy(lut_section, lut, 0x100);
    for (int i = 0; i < 0x100 / 4; i++) {
      lut_section[i] = lut_section[i] | outer_result;
    }
  }
}

/**
 * Build a 16-bit LUT from the waveform if the previous color is
 * known, e.g. all white or all black.
 * This LUT is use to look up 4 pixels at once, as with the epdiy LUT.
 */
__attribute__((optimize("O3")))
static void IRAM_ATTR waveform_lut_static_from(
    uint8_t *lut,
    const EpdWaveformPhases *phases,
    uint8_t from,
    int frame
) {
  const uint8_t *p_lut = phases->luts + (16 * 4 * frame);

  /// index into the packed "from" row
  uint8_t fi = from >> 2;
  /// bit shift amount for the packed "from" row
  uint8_t fs = 6 - 2 * (from & 3);

  // populate the first 4096 bytes
  uint8_t v1 = 0;
  uint32_t s1 = 0;
  for (uint8_t t2 = 0; t2 < 16; t2++) {
    uint8_t v2 = ((p_lut[(t2 << 2) + fi] >> fs) & 0x03) << 4;
    uint32_t s2 = t2 << 8;
    for (uint8_t t3 = 0; t3 < 16; t3++) {
      uint8_t v3 = ((p_lut[(t3 << 2) + fi] >> fs) & 0x03) << 2;
      uint32_t s3 = t3 << 4;
      for (uint8_t t4 = 0; t4 < 16; t4++) {
        uint8_t v4 = ((p_lut[(t4 << 2) + fi] >> fs) & 0x03) << 0;
        uint32_t s4 = t4;
        lut[s1 | s2 | s3 | s4] = v1 | v2 | v3 | v4;
      }
    }
  }

  // now just copy and the first 4096 bytes and add the upper two bits
  for (uint8_t t1 = 1; t1 < 16; t1++) {
    memcpy(&lut[t1 << 12], lut, 1 << 12);
  }

  for (int i = 0; i < 16; i++) {
    uint32_t v1 = ((p_lut[(i << 2) + fi] >> fs) & 0x03);
    uint32_t mask = (v1 << 30) | (v1 << 22) | (v1 << 14) | (v1 << 6);
    for (int j = 0; j < 16 * 16 * 16 / 4; j++) {
      ((uint32_t *)lut)[(i << 10) + j] |= mask;
    }
  }
}

/**
 * Set all pixels not in [xmin,xmax) to nop in the current line buffer.
 */
__attribute__((optimize("O3")))
void mask_line_buffer(uint8_t* lb, int xmin, int xmax) {
  // lower bound to where byte order is not an issue.
  int memset_start = (xmin / 16) * 4;
  int memset_end = min(((xmax + 15) / 16) * 4, EPD_LINE_BYTES);

  // memset the areas where order is not an issue
  memset(lb, 0, memset_start);
  memset(lb + memset_end, 0, EPD_LINE_BYTES - memset_end);

  const int offset_table[4] = {2, 3, 0, 1};

  // mask unused pixels at the start of the output interval
  uint8_t line_start_mask = 0xFF << (2 * (xmin % 4));
  uint8_t line_end_mask = 0xFF >> (8 - 2 * (xmax % 4));

  // number of full bytes to mask
  int lower_full_bytes = max(0, (xmin / 4 - memset_start));
  int upper_full_bytes = max(0, (memset_end - ((xmax + 3) / 4)));
  assert(lower_full_bytes <= 3);
  assert(upper_full_bytes <= 3);
  assert(memset_end >= 4);

  // mask full bytes
  for (int i = 0; i < lower_full_bytes; i++) {
    lb[memset_start + offset_table[i]] = 0x0;
  }
  for (int i = 0; i < upper_full_bytes; i++) {
    lb[memset_end - 4 + offset_table[3 - i]] = 0x0;
  }

  // mask partial bytes
  if ((memset_start + lower_full_bytes) * 4 < xmin) {
    lb[memset_start + offset_table[lower_full_bytes]] &= line_start_mask;
  }
  if ((memset_end - upper_full_bytes) * 4 > xmax) {
    lb[memset_end - 4 + offset_table[3 - upper_full_bytes]] &=
        line_end_mask;
  }
}


__attribute__((optimize("O3")))
enum EpdDrawError IRAM_ATTR calculate_lut(
    uint8_t* lut,
    int lut_size,
    enum EpdDrawMode mode,
    int frame,
    const EpdWaveformPhases* phases
) {

  enum EpdDrawMode selected_mode = mode & 0x3F;

  // two pixel per byte packing with only target color
  if (lut_size == (1 << 16)) {
      if (mode & MODE_PACKING_2PPB) {
        if (mode & PREVIOUSLY_WHITE) {
            waveform_lut_static_from(lut, phases, 0x0F, frame);
        } else if (mode & PREVIOUSLY_BLACK) {
            waveform_lut_static_from(lut, phases, 0x00, frame);
        } else {
            waveform_lut(lut, phases, frame);
        }
      // one pixel per byte with from and to colors
      } else if (mode & MODE_PACKING_1PPB_DIFFERENCE) {
        waveform_lut_64k(lut, phases, frame);
      } else {
        return EPD_DRAW_LOOKUP_NOT_IMPLEMENTED;
      }

  // 1bit per pixel monochrome with only target color
  } else if (mode & MODE_PACKING_8PPB &&
             selected_mode == MODE_EPDIY_MONOCHROME) {
    // FIXME: Pack into waveform?
    if (mode & PREVIOUSLY_WHITE) {
      memcpy(lut, lut_1bpp_black, sizeof(lut_1bpp_black));
    } else if (mode & PREVIOUSLY_BLACK) {
      // FIXME: implement!
      // memcpy(render_context.conversion_lut, lut_1bpp_white, sizeof(lut_1bpp_white));
      return EPD_DRAW_LOOKUP_NOT_IMPLEMENTED;
    } else {
      return EPD_DRAW_LOOKUP_NOT_IMPLEMENTED;
    }

    // unknown format.
  } else {
    return EPD_DRAW_LOOKUP_NOT_IMPLEMENTED;
  }
  return EPD_DRAW_SUCCESS;
}

// void IRAM_ATTR feed_display(OutputParams *params) {
// #ifdef CONFIG_EPD_BOARD_S3_PROTOTYPE
//   uint8_t line_buf[EPD_LINE_BYTES];
// #endif
//
//   while (true) {
//     xSemaphoreTake(params->start_smphr, portMAX_DELAY);
//
//     skipping = 0;
//     EpdRect area = params->area;
//     enum EpdDrawMode mode = params->mode;
//     int frame_time = params->frame_time;
//
//     params->error |= calculate_lut(params);
//
//     void (*input_calc_func)(const uint32_t *, uint8_t *, const uint8_t *) =
//         NULL;
//     if (mode & MODE_PACKING_2PPB) {
//       if (params->conversion_lut_size == 1024) {
//         if (mode & PREVIOUSLY_WHITE) {
//           input_calc_func = &calc_epd_input_4bpp_1k_lut_white;
//         } else if (mode & PREVIOUSLY_BLACK) {
//           input_calc_func = &calc_epd_input_4bpp_1k_lut_black;
//         } else {
//           params->error |= EPD_DRAW_LOOKUP_NOT_IMPLEMENTED;
//         }
//       } else if (params->conversion_lut_size == (1 << 16)) {
//         input_calc_func = &calc_epd_input_4bpp_lut_64k;
//       } else {
//         params->error |= EPD_DRAW_LOOKUP_NOT_IMPLEMENTED;
//       }
//     } else if (mode & MODE_PACKING_1PPB_DIFFERENCE) {
//       input_calc_func = &calc_epd_input_1ppB;
//     } else if (mode & MODE_PACKING_8PPB) {
//       input_calc_func = &calc_epd_input_1bpp;
//     } else {
//       params->error |= EPD_DRAW_LOOKUP_NOT_IMPLEMENTED;
//     }
//
//     // Adjust min and max row for crop.
//     const bool crop = (params->crop_to.width > 0 && params->crop_to.height > 0);
//     int crop_y = (crop ? params->crop_to.y : 0);
//     int min_y = area.y + crop_y;
//     int max_y = min(min_y + (crop ? params->crop_to.height : area.height), area.height);
//
//     // interval of the output line that is needed
//     // FIXME: only lookup needed parts
//     int line_start_x = area.x + (crop ? params->crop_to.x : 0);
//     int line_end_x = line_start_x + (crop ? params->crop_to.width : area.width);
//     line_start_x = min(max(line_start_x, 0), epd_width);
//     line_end_x = min(max(line_end_x, 0), epd_width);
//
// #ifndef CONFIG_EPD_BOARD_S3_PROTOTYPE
//     uint64_t now = esp_timer_get_time();
//     uint64_t diff = (now - last_frame_start) / 1000;
//     if (diff < MINIMUM_FRAME_TIME) {
//       vTaskDelay(MINIMUM_FRAME_TIME - diff);
//     }
// #endif
//
//     last_frame_start = esp_timer_get_time();
//
//     epd_start_frame();
//     for (int i = 0; i < FRAME_LINES; i++) {
//       if (i < min_y || i >= max_y || (params->drawn_lines != NULL && !params->drawn_lines[i - area.y])) {
// #ifndef CONFIG_EPD_BOARD_S3_PROTOTYPE
//         skip_row(frame_time);
// #else
//         memset(line_buf, 0x00, EPD_LINE_BYTES);
//         xQueueSendToBack(*params->display_queue, line_buf, portMAX_DELAY);
// #endif
//         continue;
//       }
//
//       uint8_t output[epd_width];
//       xQueueReceive(*params->pixel_queue, output, portMAX_DELAY);
// #ifndef CONFIG_EPD_BOARD_S3_PROTOTYPE
//       uint8_t* line_buf = epd_get_current_buffer();
// #endif
//
//       if (!params->error) {
//         (*input_calc_func)((uint32_t *)output, line_buf, params->conversion_lut);
//         if (line_start_x > 0 || line_end_x < epd_width) {
//           mask_line_buffer(line_buf, line_start_x, line_end_x);
//         }
//       }
//         gpio_set_level(15, 0);
//       write_row(frame_time);
// #ifdef CONFIG_EPD_BOARD_S3_PROTOTYPE
//       xQueueSendToBack(*params->display_queue, line_buf, portMAX_DELAY);
// #endif
//         gpio_set_level(15, 1);
//     }
//     if (!skipping) {
//       // Since we "pipeline" row output, we still have to latch out the last
//       // row.
//       write_row(frame_time);
//     }
//     epd_end_frame();
//
//     if (params->done_cb != NULL) {
//         params->done_cb();
//     }
//   }
// }
