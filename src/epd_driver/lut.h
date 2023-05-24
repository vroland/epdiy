#pragma once

#include "esp_attr.h"
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "epd_driver.h"

// number of bytes needed for one line of EPD pixel data.
#define EPD_LINE_BYTES EPD_WIDTH / 4

///////////////////////////// Utils /////////////////////////////////////

/*
 * Reorder the output buffer to account for I2S FIFO order.
 */
void reorder_line_buffer(uint32_t *line_data);

typedef struct {
  int thread_id;
  const uint8_t *data_ptr;
  EpdRect crop_to;
  void (*done_cb)(void);
  SemaphoreHandle_t start_smphr;
  EpdRect area;
  int frame;
  /// index of the waveform mode when using vendor waveforms.
  /// This is not necessarily the mode number if the waveform header
  //only contains a selection of modes!
  int waveform_index;
  /// waveform range when using vendor waveforms
  int waveform_range;
  /// Draw time for the current frame in 1/10ths of us.
  int frame_time;
  const EpdWaveform* waveform;
  enum EpdDrawMode mode;
  enum EpdDrawError error;
  const bool *drawn_lines;
  // Queue of input data lines
  QueueHandle_t* pixel_queue;
  // Queue of display data lines
  QueueHandle_t* display_queue;

  // Lookup table size.
  size_t conversion_lut_size;
  // Lookup table space.
  uint8_t* conversion_lut;
} OutputParams;


//void feed_display(OutputParams *params);
//void provide_out(OutputParams *params);


void bit_shift_buffer_right(uint8_t *buf, uint32_t len, int shift);
void mask_line_buffer(uint8_t* lb, int xmin, int xmax);
void nibble_shift_buffer_right(uint8_t *buf, uint32_t len);

void calc_epd_input_1ppB(const uint32_t *ld, uint8_t *epd_input, const uint8_t *conversion_lut, uint32_t epd_width);
void calc_epd_input_1ppB_64k(const uint32_t *ld, uint8_t *epd_input, const uint8_t *conversion_lut, uint32_t epd_width);

uint8_t lookup_pixels_4bpp_1k(uint16_t in, const uint8_t *conversion_lut, uint8_t from, uint32_t epd_width);
void calc_epd_input_1bpp(const uint32_t *line_data, uint8_t *epd_input, const uint8_t *lut, uint32_t epd_width);
void calc_epd_input_4bpp_1k_lut_white(const uint32_t *ld, uint8_t *epd_input, const uint8_t *conversion_lut, uint32_t epd_width);
void calc_epd_input_4bpp_1k_lut_black(const uint32_t *ld, uint8_t *epd_input, const uint8_t *conversion_lut, uint32_t epd_width);
void calc_epd_input_4bpp_1k_lut(const uint32_t *ld, uint8_t *epd_input, const uint8_t *conversion_lut, uint8_t from, uint32_t epd_width);

void calc_epd_input_4bpp_lut_64k(const uint32_t *line_data, uint8_t *epd_input, const uint8_t *conversion_lut, uint32_t epd_width);

enum EpdDrawError calculate_lut(
    uint8_t* lut,
    int lut_size,
    enum EpdDrawMode mode,
    int frame,
    const EpdWaveformPhases* phases
);

extern const uint32_t lut_1bpp_black[256];
