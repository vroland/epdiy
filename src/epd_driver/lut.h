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


void feed_display(OutputParams *params);
void provide_out(OutputParams *params);


void write_row(uint32_t output_time_dus);
void skip_row(uint8_t pipeline_finish_time);

void mask_line_buffer(uint8_t* lb, int xmin, int xmax);
enum EpdDrawError calculate_lut(OutputParams *params);
void calc_epd_input_1ppB(const uint32_t *ld, uint8_t *epd_input, const uint8_t *conversion_lut);
inline uint8_t lookup_pixels_4bpp_1k(uint16_t in, const uint8_t *conversion_lut, uint8_t from);
void calc_epd_input_4bpp_1k_lut(const uint32_t *ld, uint8_t *epd_input, const uint8_t *conversion_lut, uint8_t from);
void calc_epd_input_1bpp(const uint32_t *line_data, uint8_t *epd_input, const uint8_t *lut);
void calc_epd_input_4bpp_1k_lut_white(const uint32_t *ld, uint8_t *epd_input, const uint8_t *conversion_lut);
void calc_epd_input_4bpp_1k_lut_black(const uint32_t *ld, uint8_t *epd_input, const uint8_t *conversion_lut);
void calc_epd_input_4bpp_lut_64k(const uint32_t *line_data, uint8_t *epd_input, const uint8_t *conversion_lut);
