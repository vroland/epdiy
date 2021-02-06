#pragma once

#include "esp_attr.h"
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "epd_driver.h"

#define EPD_WIDTH 1200
#define EPD_HEIGHT 825
// number of bytes needed for one line of EPD pixel data.
#define EPD_LINE_BYTES EPD_WIDTH / 4

///////////////////////////// Looking up EPD Pixels ///////////////////////////////
void IRAM_ATTR calc_epd_input_1bpp(const uint32_t *line_data,
                                   uint8_t *epd_input, const uint8_t *lut);
void IRAM_ATTR calc_epd_input_4bpp(const uint32_t *line_data,
                                   uint8_t *epd_input,
                                   const uint8_t *conversion_lut);
void IRAM_ATTR calc_epd_input_1ppB(const uint32_t *ld, uint8_t *epd_input,
                                   const uint8_t *conversion_lut);


///////////////////////////// Calculate Lookup Tables ///////////////////////////////

void IRAM_ATTR update_epdiy_lut(uint8_t *lut_mem, uint8_t k);
void IRAM_ATTR waveform_lut(const epd_waveform_info_t *waveform, uint8_t *lut, uint8_t mode, int range, int frame);
void IRAM_ATTR waveform_lut_static_from(const epd_waveform_info_t *waveform, uint8_t *lut, uint8_t from, uint8_t mode, int range, int frame);

///////////////////////////// Utils /////////////////////////////////////

/*
 * Reorder the output buffer to account for I2S FIFO order.
 */
void IRAM_ATTR reorder_line_buffer(uint32_t *line_data);

/**
 * bit-shift a buffer `shift` <= 7 bits to the right.
 */
void IRAM_ATTR bit_shift_buffer_right(uint8_t *buf, uint32_t len, int shift);

/**
 * shift a nibble to the right.
 */
void IRAM_ATTR nibble_shift_buffer_right(uint8_t *buf, uint32_t len);

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
  const epd_waveform_info_t* waveform;
  enum EpdDrawMode mode;
  enum EpdDrawError error;
  const bool *drawn_lines;
  // Queue of input data lines
  QueueHandle_t* output_queue;
} OutputParams;


void IRAM_ATTR feed_display(OutputParams *params);
void IRAM_ATTR provide_out(OutputParams *params);


void IRAM_ATTR write_row(uint32_t output_time_dus);
void IRAM_ATTR skip_row(uint8_t pipeline_finish_time);
