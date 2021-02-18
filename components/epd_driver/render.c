#include "epd_temperature.h"
#include "display_ops.h"
#include "epd_driver.h"
#include "lut.h"

#include "driver/rtc_io.h"
#include "esp_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "xtensa/core-macros.h"
#include <string.h>

inline uint32_t min(uint32_t x, uint32_t y) { return x < y ? x : y; }

const int clear_cycle_time = 12;

#define RTOS_ERROR_CHECK(x)                                                    \
  do {                                                                         \
    esp_err_t __err_rc = (x);                                                  \
    if (__err_rc != pdPASS) {                                                  \
      abort();                                                                 \
    }                                                                          \
  } while (0)

#define CLEAR_BYTE 0B10101010
#define DARK_BYTE 0B01010101

// Queue of input data lines
static QueueHandle_t output_queue;

static OutputParams fetch_params;
static OutputParams feed_params;


void epd_push_pixels(EpdRect area, short time, int color) {

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


///////////////////////////// Coordination ///////////////////////////////


/**
 * Find the waveform temperature range index for a given temperature in °C.
 * If no range in the waveform data fits the given temperature, return the
 * closest one.
 * Returns -1 if the waveform does not contain any temperature range.
 */
int waveform_temp_range_index(const epd_waveform_info_t* waveform, int temperature) {
    int idx = 0;
    if (waveform->num_temp_ranges == 0) {
        return -1;
    }
    while (idx < waveform->num_temp_ranges - 1
            && waveform->temp_intervals[idx].min < temperature) {
        idx++;
    }
    return idx;
}

////////////////////////////////  API Procedures //////////////////////////////////

enum EpdDrawError IRAM_ATTR epd_draw_base(EpdRect area,
                            const uint8_t *data,
                            EpdRect crop_to,
                            enum EpdDrawMode mode,
                            int temperature,
                            const bool *drawn_lines,
                            const epd_waveform_info_t *waveform) {
  uint8_t line[EPD_WIDTH / 2];
  memset(line, 255, EPD_WIDTH / 2);

  int waveform_range = waveform_temp_range_index(waveform, temperature);
  int waveform_mode = 0;
  uint8_t frame_count = 0;
  if (mode & EPDIY_WAVEFORM) {
    frame_count = 15;
  } else if (mode & VENDOR_WAVEFORM) {
    waveform_mode = mode & 0x0F;
    frame_count = waveform->mode_data[waveform_mode]
                      ->range_data[waveform_range]
                      ->phases;
  }

  const bool crop = (crop_to.width > 0 && crop_to.height > 0);
  if (crop && (crop_to.width > area.width
              || crop_to.height > area.height
              || crop_to.x > area.width
              || crop_to.y > area.height)) {
      return DRAW_INVALID_CROP;
  }

  for (uint8_t k = 0; k < frame_count; k++) {
    uint64_t frame_start = esp_timer_get_time() / 1000;
    fetch_params.area = area;
    // IMPORTANT: This must only ever read from PSRAM,
    //            Since the PSRAM workaround is disabled for lut.c
    fetch_params.data_ptr = data;
    fetch_params.crop_to = crop_to;
    fetch_params.frame = k;
    fetch_params.waveform_range = waveform_range;
    fetch_params.waveform_mode = waveform_mode;
    fetch_params.mode = mode;
    fetch_params.waveform = waveform;
    fetch_params.error = DRAW_SUCCESS;
    fetch_params.drawn_lines = drawn_lines;
    fetch_params.output_queue = &output_queue;

    feed_params.area = area;
    feed_params.data_ptr = data;
    feed_params.crop_to = crop_to;
    feed_params.frame = k;
    feed_params.waveform_range = waveform_range;
    feed_params.waveform_mode = waveform_mode;
    feed_params.mode = mode;
    feed_params.waveform = waveform;
    feed_params.error = DRAW_SUCCESS;
    feed_params.drawn_lines = drawn_lines;
    feed_params.output_queue = &output_queue;

    xSemaphoreGive(fetch_params.start_smphr);
    xSemaphoreGive(feed_params.start_smphr);
    xSemaphoreTake(fetch_params.done_smphr, portMAX_DELAY);
    xSemaphoreTake(feed_params.done_smphr, portMAX_DELAY);

    uint64_t frame_end = esp_timer_get_time() / 1000;
    if (frame_end - frame_start < MINIMUM_FRAME_TIME) {
      vTaskDelay(min(MINIMUM_FRAME_TIME - (frame_end - frame_start),
                     MINIMUM_FRAME_TIME));
    }
    enum EpdDrawError all_errors = fetch_params.error | feed_params.error;
    if (all_errors != DRAW_SUCCESS) {
        return all_errors;
    }
  }
  return DRAW_SUCCESS;
}

void epd_clear_area(EpdRect area) {
  epd_clear_area_cycles(area, 3, clear_cycle_time);
}

void epd_clear_area_cycles(EpdRect area, int cycles, int cycle_time) {
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

  //conversion_lut = (uint8_t *)heap_caps_malloc(1 << 16, MALLOC_CAP_8BIT);
  //assert(conversion_lut != NULL);
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


EpdRect epd_difference_image_base(
    const uint8_t* to,
    const uint8_t* from,
    EpdRect crop_to,
    int fb_width,
    int fb_height,
    uint8_t* interlaced,
    bool* dirty_lines
) {
    uint8_t dirty_cols[EPD_WIDTH] = {0};
    int x_end = min(fb_width, crop_to.x + crop_to.width);
    int y_end = min(fb_height, crop_to.y + crop_to.height);

    for (int y=crop_to.y; y < fb_height; y++) {
        uint8_t dirty = 0;
        for (int x = crop_to.x; x < fb_width; x++) {
            uint8_t t = *(to + y*fb_width / 2 + x / 2);
            t = (x % 2) ? (t >> 4) : (t & 0x0f);
            uint8_t f = *(from + y*fb_width / 2+ x / 2);
            f = (x % 2) ? ((f >> 4) & 0x0f) : (f & 0x0f);
            dirty |= (t ^ f);
            dirty_cols[x] |= (t ^ f);
            interlaced[y * fb_width + x] = (t << 4) | f;
        }
        dirty_lines[y] = dirty > 0;
    }
    int min_x, min_y, max_x, max_y;
    for (min_x = crop_to.x; min_x < x_end; min_x++) {
      if (dirty_cols[min_x] != 0) break;
    }
    for (max_x = x_end - 1; max_x >= crop_to.x; max_x--) {
      if (dirty_cols[max_x] != 0) break;
    }
    for (min_y = crop_to.y; min_y < y_end; min_y++) {
      if (dirty_lines[min_y] != 0) break;
    }
    for (max_y = y_end - 1; max_y >= crop_to.y; max_y--) {
      if (dirty_lines[max_y] != 0) break;
    }
    EpdRect crop_rect = {
      .x = min_x,
      .y = min_y,
      .width = max_x - min_x + 1,
      .height = max_y - min_y + 1
    };
    return crop_rect;
}

EpdRect epd_difference_image(
    const uint8_t* to,
    const uint8_t* from,
    uint8_t* interlaced,
    bool* dirty_lines
) {
  return epd_difference_image_base(to, from, epd_full_screen(), EPD_WIDTH, EPD_HEIGHT, interlaced, dirty_lines);
}

EpdRect epd_difference_image_cropped(
    const uint8_t* to,
    const uint8_t* from,
    EpdRect crop_to,
    uint8_t* interlaced,
    bool* dirty_lines
) {
  return epd_difference_image_base(to, from, crop_to, EPD_WIDTH, EPD_HEIGHT, interlaced, dirty_lines);
}
