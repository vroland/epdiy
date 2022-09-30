#include "epd_temperature.h"

#ifndef CONFIG_EPD_BOARD_S3_PROTOTYPE
#include "display_ops.h"
#endif
#include "epd_driver.h"
#include "include/epd_driver.h"
#include "include/epd_internals.h"
#include "include/epd_board.h"
#include "lut.h"
#include "s3_lcd.h"

#include "esp_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "xtensa/core-macros.h"
#include <string.h>

inline int min(int x, int y) { return x < y ? x : y; }
inline int max(int x, int y) { return x > y ? x : y; }

const int clear_cycle_time = 12;

const int DEFAULT_FRAME_TIME = 120;

#define RTOS_ERROR_CHECK(x)                                                    \
  do {                                                                         \
    esp_err_t __err_rc = (x);                                                  \
    if (__err_rc != pdPASS) {                                                  \
      abort();                                                                 \
    }                                                                          \
  } while (0)

#define CLEAR_BYTE 0B10101010
#define DARK_BYTE 0B01010101

static SemaphoreHandle_t update_done;

// Queue of input data lines
static QueueHandle_t pixel_queue;

#ifdef CONFIG_EPD_BOARD_S3_PROTOTYPE
// Queue of display data lines
static QueueHandle_t display_queue;
#endif

static OutputParams fetch_params;
static OutputParams feed_params;

typedef struct {
    int k;
    int framecount;
    const int* phase_times;
} RenderContext_t;

static RenderContext_t render_context;


// Space to use for the EPD output lookup table, which
// is calculated for each cycle.
static uint8_t* conversion_lut;

#ifndef CONFIG_EPD_BOARD_S3_PROTOTYPE
void epd_push_pixels(EpdRect area, short time, int color) {

  uint8_t row[EPD_LINE_BYTES] = {0};

  const uint8_t color_choice[4] = {DARK_BYTE, CLEAR_BYTE, 0x00, 0xFF};
  for (uint32_t i = 0; i < area.width; i++) {
    uint32_t position = i + area.x % 4;
    uint8_t mask = color_choice[color] & (0b00000011 << (2 * (position % 4)));
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
#else
void epd_push_pixels(EpdRect area, short time, int color) {
    const uint8_t color_choice[4] = {DARK_BYTE, CLEAR_BYTE, 0x00, 0xFF};
    output_singlecolor_frame(area, time, color_choice[color]);
}

#endif


///////////////////////////// Coordination ///////////////////////////////


/**
 * Find the waveform temperature range index for a given temperature in °C.
 * If no range in the waveform data fits the given temperature, return the
 * closest one.
 * Returns -1 if the waveform does not contain any temperature range.
 */
int waveform_temp_range_index(const EpdWaveform* waveform, int temperature) {
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

static int get_waveform_index(const EpdWaveform* waveform, enum EpdDrawMode mode) {
    for (int i=0; i < waveform->num_modes; i++) {
        if (waveform->mode_data[i]->type == (mode & 0x3F)) {
            return i;
        }
    }
    return -1;
}

static void IRAM_ATTR on_frame_prepare() {
	int frame_time = DEFAULT_FRAME_TIME;
	if (render_context.phase_times != NULL) {
		frame_time = render_context.phase_times[render_context.k];
	}

    //if (mode & MODE_EPDIY_MONOCHROME) {
    //    frame_time = MONOCHROME_FRAME_TIME;
    //}

    if (render_context.k == 0) {
        s3_lcd_enable_data_out(pixel_queue);
    }

    BaseType_t task_awoken = false;
    fetch_params.frame = render_context.k;
    fetch_params.frame_time = frame_time;
    feed_params.frame = render_context.k;
    feed_params.frame_time = frame_time;
    render_context.k++;

    if (render_context.k >= render_context.framecount) {
        s3_delete_frame_prepare_cb();
        s3_lcd_disable_data_out(0x00);
        xSemaphoreGiveFromISR(update_done, &task_awoken);
    } else {
        xSemaphoreGiveFromISR(feed_params.start_smphr, &task_awoken);
        xSemaphoreGiveFromISR(fetch_params.start_smphr, &task_awoken);
    }
    portYIELD_FROM_ISR();
}

enum EpdDrawError IRAM_ATTR epd_draw_base(EpdRect area,
                            const uint8_t *data,
                            EpdRect crop_to,
                            enum EpdDrawMode mode,
                            int temperature,
                            const bool *drawn_lines,
                            const EpdWaveform *waveform) {
  uint8_t line[EPD_WIDTH / 2];
  memset(line, 255, EPD_WIDTH / 2);

  int waveform_range = waveform_temp_range_index(waveform, temperature);
  if (waveform_range < 0) {
    return EPD_DRAW_NO_PHASES_AVAILABLE;
  }
  int waveform_index = 0;
  uint8_t frame_count = 0;
  const EpdWaveformPhases* waveform_phases = NULL;

  // no waveform required for monochrome mode
  if (!(mode & MODE_EPDIY_MONOCHROME)) {
      waveform_index = get_waveform_index(waveform, mode);
      if (waveform_index < 0) {
        return EPD_DRAW_MODE_NOT_FOUND;
      }

      waveform_phases = waveform->mode_data[waveform_index]
                                  ->range_data[waveform_range];
       // FIXME: error if not present
      frame_count = waveform_phases->phases;
  } else {
      frame_count = 1;
  }

  if (crop_to.width < 0 || crop_to.height < 0) {
      return EPD_DRAW_INVALID_CROP;
  }

  const bool crop = (crop_to.width > 0 && crop_to.height > 0);
  if (crop && (crop_to.width > area.width
              || crop_to.height > area.height
              || crop_to.x > area.width
              || crop_to.y > area.height)) {
      return EPD_DRAW_INVALID_CROP;
  }

    fetch_params.area = area;
    fetch_params.crop_to = crop_to;
    fetch_params.waveform_range = waveform_range;
    fetch_params.waveform_index = waveform_index;
    fetch_params.mode = mode;
    fetch_params.waveform = waveform;
    fetch_params.error = EPD_DRAW_SUCCESS;
    fetch_params.drawn_lines = drawn_lines;
    fetch_params.pixel_queue = &pixel_queue;
    fetch_params.display_queue = NULL;
    fetch_params.data_ptr = data;
    fetch_params.done_cb = NULL;

    feed_params.area = area;
    feed_params.crop_to = crop_to;
    feed_params.waveform_range = waveform_range;
    feed_params.waveform_index = waveform_index;
    feed_params.mode = mode;
    feed_params.waveform = waveform;
    feed_params.error = EPD_DRAW_SUCCESS;
    feed_params.drawn_lines = drawn_lines;
    feed_params.pixel_queue = &pixel_queue;
    feed_params.display_queue = NULL;
    feed_params.data_ptr = data;
    feed_params.done_cb = NULL;

    render_context.k = 0;
    render_context.framecount = frame_count;
    render_context.phase_times = NULL;
	if (waveform_phases != NULL && waveform_phases->phase_times != NULL) {
		render_context.phase_times = waveform_phases->phase_times;
	}

    ESP_LOGI("epdiy", "starting update, phases: %d", frame_count);
#ifdef CONFIG_EPD_BOARD_S3_PROTOTYPE
    feed_params.display_queue = &display_queue;
    //feed_params.done_cb = on_frame_prepare;
    s3_set_frame_prepare_cb(on_frame_prepare);
    xSemaphoreTake(update_done, portMAX_DELAY);
    ESP_LOGI("epdiy", "transfer done");
#else

  for (uint8_t k = 0; k < frame_count; k++) {


    // IMPORTANT: This must only ever read from PSRAM,
    //            Since the PSRAM workaround is disabled for lut.c
    fetch_params.data_ptr = data;
    fetch_params.frame = k;

    feed_params.frame = k;
    feed_params.frame_time = frame_time;

    xSemaphoreGive(fetch_params.start_smphr);
    xSemaphoreGive(feed_params.start_smphr);

    xSemaphoreTake(fetch_params.done_smphr, portMAX_DELAY);
    xSemaphoreTake(feed_params.done_smphr, portMAX_DELAY);
  }
#endif
  enum EpdDrawError all_errors = fetch_params.error | feed_params.error;
  if (all_errors != EPD_DRAW_SUCCESS) {
      return all_errors;
  }
  return EPD_DRAW_SUCCESS;
}

void epd_clear_area(EpdRect area) {
  epd_clear_area_cycles(area, 3, clear_cycle_time);
}

void epd_clear_area_cycles(EpdRect area, int cycles, int cycle_time) {
  const short white_time = cycle_time;
  const short dark_time = cycle_time;

  //setup_transfer();
  for (int c = 0; c < cycles; c++) {
    for (int i = 0; i < 10; i++) {
      epd_push_pixels(area, dark_time, 0);
    }
    for (int i = 0; i < 10; i++) {
      epd_push_pixels(area, white_time, 1);
    }
  }
  //stop_transfer();
}



void epd_init(enum EpdInitOptions options) {
#if defined(CONFIG_EPD_BOARD_REVISION_LILYGO_T5_47)
  epd_set_board(&epd_board_lilygo_t5_47);
#elif defined(CONFIG_EPD_BOARD_REVISION_V2_V3)
  epd_set_board(&epd_board_v2_v3);
#elif defined(CONFIG_EPD_BOARD_REVISION_V4)
  epd_set_board(&epd_board_v4);
#elif defined(CONFIG_EPD_BOARD_REVISION_V5)
  epd_set_board(&epd_board_v5);
#elif defined(CONFIG_EPD_BOARD_REVISION_V6)
  epd_set_board(&epd_board_v6);
#elif defined(CONFIG_EPD_BOARD_S3_PROTOTYPE)
  epd_set_board(&epd_board_s3_prototype);
#else
  // Either the board should be set in menuconfig or the epd_set_board() must be called before epd_init()
  assert(epd_board != NULL);
#endif

  epd_board->init(EPD_WIDTH);
#ifndef CONFIG_EPD_BOARD_S3_PROTOTYPE
  epd_hw_init(EPD_WIDTH);
#endif
  epd_temperature_init();

  size_t lut_size = 0;
  if (options & EPD_LUT_1K) {
    lut_size = 1 << 10;
  } else if ((options & EPD_LUT_64K) || (options == EPD_OPTIONS_DEFAULT)) {
    lut_size = 1 << 16;
  } else {
    ESP_LOGE("epd", "invalid init options: %d", options);
    return;
  }

  conversion_lut = (uint8_t *)heap_caps_malloc(lut_size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
  if (conversion_lut == NULL) {
    ESP_LOGE("epd", "could not allocate LUT!");
  }
  assert(conversion_lut != NULL);

  fetch_params.conversion_lut = conversion_lut;
  fetch_params.conversion_lut_size = lut_size;
  feed_params.conversion_lut = conversion_lut;
  feed_params.conversion_lut_size = lut_size;

  update_done = xSemaphoreCreateBinary();

  fetch_params.start_smphr = xSemaphoreCreateBinary();
  feed_params.start_smphr = xSemaphoreCreateBinary();

  RTOS_ERROR_CHECK(xTaskCreatePinnedToCore((void (*)(void *))provide_out,
                                           "epd_fetch", (1 << 12), &fetch_params, 5,
                                           NULL, 0));

  RTOS_ERROR_CHECK(xTaskCreatePinnedToCore((void (*)(void *))feed_display,
                                           "epd_feed", 1 << 12, &feed_params,
                                           5, NULL, 1));

  //conversion_lut = (uint8_t *)heap_caps_malloc(1 << 16, MALLOC_CAP_8BIT);
  //assert(conversion_lut != NULL);
  int queue_len = 32;
  if (options & EPD_FEED_QUEUE_32) {
    queue_len = 32;
  } else if (options & EPD_FEED_QUEUE_8) {
    queue_len = 8;
  }

  pixel_queue = xQueueCreate(queue_len, EPD_WIDTH);
#ifdef CONFIG_EPD_BOARD_S3_PROTOTYPE
  display_queue = xQueueCreate(queue_len, EPD_WIDTH / 4);
  s3_set_display_queue(display_queue);
#endif

}

EpdRect epd_difference_image_base(
    const uint8_t* to,
    const uint8_t* from,
    EpdRect crop_to,
    int fb_width,
    int fb_height,
    uint8_t* interlaced,
    bool* dirty_lines,
    uint8_t* from_or,
    uint8_t* from_and
) {
    assert(from_or != NULL);
    assert(from_and != NULL);
    // OR over all pixels of the "from"-image
    *from_or = 0x00;
    // AND over all pixels of the "from"-image
    *from_and = 0x0F;

    uint8_t dirty_cols[EPD_WIDTH] = {0};
    int x_end = min(fb_width, crop_to.x + crop_to.width);
    int y_end = min(fb_height, crop_to.y + crop_to.height);

    for (int y=crop_to.y; y < y_end; y++) {
        uint8_t dirty = 0;
        for (int x = crop_to.x; x < x_end; x++) {
            uint8_t t = *(to + y*fb_width / 2 + x / 2);
            t = (x % 2) ? (t >> 4) : (t & 0x0f);
            uint8_t f = *(from + y*fb_width / 2+ x / 2);
            f = (x % 2) ? (f >> 4) : (f & 0x0f);
            *from_or |= f;
            *from_and &= f;
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
      .width = max(max_x - min_x + 1, 0),
      .height = max(max_y - min_y + 1, 0),
    };
    return crop_rect;
}

EpdRect epd_difference_image(
    const uint8_t* to,
    const uint8_t* from,
    uint8_t* interlaced,
    bool* dirty_lines
) {
  uint8_t from_or = 0;
  uint8_t from_and = 0;
  return epd_difference_image_base(to, from, epd_full_screen(), EPD_WIDTH, EPD_HEIGHT, interlaced, dirty_lines, &from_or, &from_and);
}

EpdRect epd_difference_image_cropped(
    const uint8_t* to,
    const uint8_t* from,
    EpdRect crop_to,
    uint8_t* interlaced,
    bool* dirty_lines,
    bool* previously_white,
    bool* previously_black
) {

  uint8_t from_or, from_and;

  EpdRect result = epd_difference_image_base(to, from, crop_to, EPD_WIDTH, EPD_HEIGHT, interlaced, dirty_lines, &from_or, &from_and);

  if (previously_white != NULL) *previously_white = (from_and == 0x0F);
  if (previously_black != NULL) *previously_black = (from_or == 0x00);
  return result;
}
