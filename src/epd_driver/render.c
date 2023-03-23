#include "epd_temperature.h"

#include "display_ops.h"
#include "epd_driver.h"
#include "include/epd_driver.h"
#include "include/epd_internals.h"
#include "include/epd_board.h"
#include "lut.h"
#include "s3_lcd.h"

#include "esp_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "xtensa/core-macros.h"
#include "rom/cache.h"
#include <stdint.h>
#include <stdalign.h>
#include <stdatomic.h>
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

// When using the LCD peripheral, we may need padding lines to
// satisfy the bounce buffer size requirements
#ifndef CONFIG_EPD_BOARD_S3_PROTOTYPE
#define FRAME_LINES               EPD_HEIGHT
#else
#define FRAME_LINES              (((EPD_HEIGHT  + 7) / 8) * 8)
#endif

#define NUM_FEED_THREADS 2

typedef struct {
    int size;
    atomic_int current;
    atomic_int last;
    uint8_t* buf;
    size_t element_size;
} LineQueue_t;

uint8_t* lq_current(LineQueue_t* queue) {
    int current = atomic_load_explicit(&queue->current, memory_order_acquire);
    int last = atomic_load_explicit(&queue->last, memory_order_acquire);

    if ((current  + 1) % queue->size == last) {
        return NULL;
    }
    return &queue->buf[current * queue->element_size];
}

void lq_commit(LineQueue_t* queue) {

    int current = atomic_load_explicit(&queue->current, memory_order_acquire);
    if (current == queue->size - 1) {
        queue->current = 0;
    } else {
        atomic_fetch_add(&queue->current, 1);
    }
}

int lq_read(LineQueue_t* queue, uint8_t* dst) {
    int current = atomic_load_explicit(&queue->current, memory_order_acquire);
    int last = atomic_load_explicit(&queue->last, memory_order_acquire);

    if (current == last) {
        return -1;
    }

    memcpy(dst, &queue->buf[last * queue->element_size], queue->element_size);

    if (last == queue->size - 1) {
        queue->last = 0;
    } else {
        atomic_fetch_add(&queue->last, 1);
    }
    return 0;
}


typedef struct {
    EpdRect area;
    EpdRect crop_to;
    const bool *drawn_lines;
    const uint8_t *data_ptr;

    /// index of the next line of data to process
    atomic_int lines_prepared;
    volatile int lines_consumed;

    /// frame currently in the current update cycle
    int current_frame;
    /// number of frames in the current update cycle
    int cycle_frames;

    TaskHandle_t feed_tasks[NUM_FEED_THREADS];
    SemaphoreHandle_t feed_done_smphr[NUM_FEED_THREADS];
    SemaphoreHandle_t frame_done;

    /// index of the waveform mode when using vendor waveforms.
    /// This is not necessarily the mode number if the waveform header
    //only contains a selection of modes!
    int waveform_index;
    /// waveform range when using vendor waveforms
    int waveform_range;
    /// Draw time for the current frame in 1/10ths of us.
    int frame_time;

    const int* phase_times;

    const EpdWaveform* waveform;
    enum EpdDrawMode mode;
    enum EpdDrawError error;


    // Lookup table size.
    size_t conversion_lut_size;
    // Lookup table space.
    uint8_t* conversion_lut;

    /// Queue of lines prepared for output to the display,
    /// one for each thread.
    LineQueue_t line_queues[NUM_FEED_THREADS];
    uint8_t line_threads[FRAME_LINES];

    /// track line skipping when working in old i2s mode
    int skipping;
} RenderContext_t;

static RenderContext_t render_context;

void i2s_write_row(uint32_t output_time_dus);
void i2s_skip_row(uint8_t pipeline_finish_time);

static bool IRAM_ATTR fill_line_noop(uint8_t* line) {
    memset(line, 0x00, EPD_LINE_BYTES);
    return false;
}

static bool IRAM_ATTR fill_line_white(uint8_t* line) {
    memset(line, CLEAR_BYTE, EPD_LINE_BYTES);
    return false;
}

static bool IRAM_ATTR fill_line_black(uint8_t* line) {
    memset(line, DARK_BYTE, EPD_LINE_BYTES);
    return false;
}

#ifdef CONFIG_IDF_TARGET_ESP32S3

/// start the next frame in the current update cycle
static void IRAM_ATTR handle_lcd_frame_done() {
    epd_lcd_frame_done_cb(NULL);
    epd_lcd_line_source_cb(NULL);

    BaseType_t task_awoken = pdFALSE;
    xSemaphoreGiveFromISR(render_context.frame_done, &task_awoken);

    portYIELD_FROM_ISR();
}


void epd_push_pixels_lcd(EpdRect area, short time, int color) {
    render_context.current_frame = 0;
    epd_lcd_frame_done_cb(handle_lcd_frame_done);
    if (color == 0) {
        epd_lcd_line_source_cb(&fill_line_black);
    } else if (color == 1) {
        epd_lcd_line_source_cb(&fill_line_white);
    } else {
        epd_lcd_line_source_cb(&fill_line_noop);
    }
    epd_lcd_start_frame();
    xSemaphoreTake(render_context.frame_done, portMAX_DELAY);
}

void epd_push_pixels(EpdRect area, short time, int color) {
    epd_push_pixels_lcd(area, time, color);
}
#else

void epd_push_pixels_i2s(EpdRect area, short time, int color) {

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
      i2s_skip_row(time);
      // start area of interest: set row data
    } else if (i == area.y) {
      epd_switch_buffer();
      memcpy(epd_get_current_buffer(), row, EPD_LINE_BYTES);
      epd_switch_buffer();
      memcpy(epd_get_current_buffer(), row, EPD_LINE_BYTES);

      i2s_write_row(time * 10);
      // load nop row if done with area
    } else if (i >= area.y + area.height) {
      i2s_skip_row(time);
      // output the same as before
    } else {
      i2s_write_row(time * 10);
    }
  }
  // Since we "pipeline" row output, we still have to latch out the last row.
  i2s_write_row(time * 10);

  epd_end_frame();
}

void epd_push_pixels(EpdRect area, short time, int color) {
    epd_push_pixels_i2s(area, time, color);
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

static bool IRAM_ATTR retrieve_line_isr(uint8_t* buf) {
    if (render_context.lines_consumed >= FRAME_LINES) {
        return false;
    }
    int thread = render_context.line_threads[render_context.lines_consumed];
    assert(thread < NUM_FEED_THREADS);

    LineQueue_t* lq = &render_context.line_queues[thread];

    BaseType_t awoken = pdFALSE;
    assert(lq_read(lq, buf) == 0);
    if (render_context.lines_consumed >= EPD_HEIGHT) {
        memset(buf, 0x00, EPD_LINE_BYTES);
    }
    render_context.lines_consumed += 1;
    return awoken;
}

/// start the next frame in the current update cycle
static void IRAM_ATTR prepare_lcd_frame() {
	int frame_time = DEFAULT_FRAME_TIME;
	if (render_context.phase_times != NULL) {
		frame_time = render_context.phase_times[render_context.current_frame];
	}

    if (render_context.mode & MODE_EPDIY_MONOCHROME) {
        frame_time = MONOCHROME_FRAME_TIME;
    }
    render_context.frame_time = frame_time;


    enum EpdDrawMode mode = render_context.mode;
    const EpdWaveformPhases* phases =
        render_context.waveform->mode_data[render_context.waveform_index]->range_data[render_context.waveform_range];

    render_context.error |= calculate_lut(
            render_context.conversion_lut,
            render_context.conversion_lut_size,
            mode,
            render_context.current_frame,
            phases
    );

    render_context.lines_prepared = 0;
    render_context.lines_consumed = 0;

// on the classic ESP32, the prepare task starts the feeder task
#ifdef CONFIG_IDF_TARGET_ESP32S3
    xTaskNotifyGive(render_context.feed_tasks[!xPortGetCoreID()]);
    xTaskNotifyGive(render_context.feed_tasks[xPortGetCoreID()]);
#else
    xTaskNotifyGive(render_context.feed_tasks[0]);
    xTaskNotifyGive(render_context.feed_tasks[1]);
#endif
}

// FIXME: fix misleading naming:
//  area -> buffer dimensions
//  crop -> area taken out of buffer
enum EpdDrawError IRAM_ATTR epd_draw_base(EpdRect area,
                            const uint8_t *data,
                            EpdRect crop_to,
                            enum EpdDrawMode mode,
                            int temperature,
                            const bool *drawn_lines,
                            const EpdWaveform *waveform) {
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

    render_context.area = area;
    render_context.crop_to = crop_to;
    render_context.waveform_range = waveform_range;
    render_context.waveform_index = waveform_index;
    render_context.mode = mode;
    render_context.waveform = waveform;
    render_context.error = EPD_DRAW_SUCCESS;
    render_context.drawn_lines = drawn_lines;
    render_context.data_ptr = data;

    render_context.lines_prepared = 0;
    render_context.lines_consumed = 0;
    render_context.current_frame = 0;
    render_context.cycle_frames = frame_count;
    render_context.phase_times = NULL;
	if (waveform_phases != NULL && waveform_phases->phase_times != NULL) {
		render_context.phase_times = waveform_phases->phase_times;
	}

    ESP_LOGI("epdiy", "starting update, phases: %d", frame_count);

    epd_set_mode(1);

    for (uint8_t k = 0; k < frame_count; k++) {
#ifdef CONFIG_EPD_BOARD_S3_PROTOTYPE
        epd_lcd_frame_done_cb(handle_lcd_frame_done);
#endif
        prepare_lcd_frame();
        // transmission started in renderer threads
        xSemaphoreTake(render_context.frame_done, portMAX_DELAY);

        for (int i=0; i<NUM_FEED_THREADS; i++) {
            xSemaphoreTake(render_context.feed_done_smphr[i], portMAX_DELAY);
        }

        render_context.current_frame++;

        // make the watchdog happy.
        if (k % 10 == 0) {
            vTaskDelay(0);
        }
    }

#ifdef CONFIG_EPD_BOARD_S3_PROTOTYPE
    epd_lcd_line_source_cb(NULL);
    epd_lcd_frame_done_cb(NULL);
#endif

   epd_set_mode(1);

  //for (uint8_t k = 0; k < frame_count; k++) {


  //  // IMPORTANT: This must only ever read from PSRAM,
  //  //            Since the PSRAM workaround is disabled for lut.c
  //  fetch_params.data_ptr = data;
  //  fetch_params.frame = k;

  //  feed_params.frame = k;
  //  feed_params.frame_time = frame_time;

  //  xSemaphoreGive(fetch_params.start_smphr);
  //  xSemaphoreGive(feed_params.start_smphr);

  //  xSemaphoreTake(fetch_params.done_smphr, portMAX_DELAY);
  //  xSemaphoreTake(feed_params.done_smphr, portMAX_DELAY);
  //}
  if (render_context.error != EPD_DRAW_SUCCESS) {
      return render_context.error;
  }
  return EPD_DRAW_SUCCESS;
}

//output a row to the display.
void IRAM_ATTR i2s_write_row(uint32_t output_time_dus) {
  epd_output_row(output_time_dus);
  render_context.skipping = 0;
}


// skip a display row
void IRAM_ATTR i2s_skip_row(uint8_t pipeline_finish_time) {
  // output previously loaded row, fill buffer with no-ops.
  if (render_context.skipping < 2) {
    memset(epd_get_current_buffer(), 0x00, EPD_LINE_BYTES);
    epd_output_row(pipeline_finish_time);
  } else {
    epd_skip();
  }
  render_context.skipping++;
}

typedef void (*lut_func_t)(const uint32_t*, uint8_t*, const uint8_t*);

lut_func_t get_lut_function() {
    const enum EpdDrawMode mode = render_context.mode;
    if (mode & MODE_PACKING_2PPB) {
      if (render_context.conversion_lut_size == 1024) {
        if (mode & PREVIOUSLY_WHITE) {
          return &calc_epd_input_4bpp_1k_lut_white;
        } else if (mode & PREVIOUSLY_BLACK) {
          return &calc_epd_input_4bpp_1k_lut_black;
        } else {
          render_context.error |= EPD_DRAW_LOOKUP_NOT_IMPLEMENTED;
        }
      } else if (render_context.conversion_lut_size == (1 << 16)) {
        return &calc_epd_input_4bpp_lut_64k;
      } else {
        render_context.error |= EPD_DRAW_LOOKUP_NOT_IMPLEMENTED;
      }
    } else if (mode & MODE_PACKING_1PPB_DIFFERENCE) {
      if (render_context.conversion_lut_size == 1024) {
          return &calc_epd_input_1ppB;
      } else {
          return &calc_epd_input_1ppB_64k;
      }
    } else if (mode & MODE_PACKING_8PPB) {
      return &calc_epd_input_1bpp;
    } else {
      render_context.error |= EPD_DRAW_LOOKUP_NOT_IMPLEMENTED;
    }
    return NULL;
}

void IRAM_ATTR feed_display(int thread_id) {
  uint8_t line_buf[EPD_LINE_BYTES];
  uint8_t input_line[EPD_WIDTH];

  ESP_LOGI("epdiy", "thread id: %d", thread_id);

  while (true) {
    // line must be able to hold 2-pixel-per-byte or 1-pixel-per-byte data
    memset(input_line, 255, EPD_WIDTH);

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    //xSemaphoreTake(render_context.frame_start_smphr[thread_id], portMAX_DELAY);

    EpdRect area = render_context.area;
    const enum EpdDrawMode mode = render_context.mode;
    const EpdRect crop_to = render_context.crop_to;
    const bool horizontally_cropped = !(crop_to.x == 0 && crop_to.width == area.width);
    const bool vertically_cropped = !(crop_to.y == 0 && crop_to.height == area.height);

    lut_func_t input_calc_func = get_lut_function();

    // number of pixels per byte of input data
    int ppB = 0;
    int bytes_per_line = 0;
    int width_divider = 0;

    if (mode & MODE_PACKING_1PPB_DIFFERENCE) {
      ppB = 1;
      bytes_per_line = area.width;
      width_divider = 1;
    } else if (mode & MODE_PACKING_2PPB) {
      ppB = 2;
      bytes_per_line = area.width / 2 + area.width % 2;
      width_divider = 2;
    } else if (mode & MODE_PACKING_8PPB) {
      ppB = 8;
      bytes_per_line = (area.width / 8 + (area.width % 8 > 0));
      width_divider = 8;
    } else {
      render_context.error |= EPD_DRAW_INVALID_PACKING_MODE;
    }

    int crop_x = (horizontally_cropped ? crop_to.x : 0);
    int crop_w = (horizontally_cropped ? crop_to.width : 0);
    int crop_y = (vertically_cropped ? crop_to.y : 0);
    int crop_h = (vertically_cropped ? crop_to.height : 0);

    const uint8_t *ptr_start = render_context.data_ptr;

    // Adjust for negative starting coordinates with optional crop
    if (area.x - crop_x < 0) {
      ptr_start += -(area.x - crop_x) / width_divider;
    }

    if (area.y - crop_y < 0) {
      ptr_start += -(area.y - crop_y) * bytes_per_line;
    }

    // interval of the output line that is needed
    // FIXME: only lookup needed parts
    int line_start_x = area.x + (horizontally_cropped ? crop_to.x : 0);
    int line_end_x = line_start_x + (horizontally_cropped ? crop_to.width : area.width);
    line_start_x = min(max(line_start_x, 0), EPD_WIDTH);
    line_end_x = min(max(line_end_x, 0), EPD_WIDTH);

    // calculate start and end row with crop
    int min_y = area.y + crop_y;
    int max_y = min(min_y + (vertically_cropped ? crop_h : area.height), area.height);

    LineQueue_t* lq = &render_context.line_queues[thread_id];

    int l = 0;
    while (l = atomic_fetch_add(&render_context.lines_prepared, 1), l < FRAME_LINES) {
      //if (thread_id) gpio_set_level(15, 0);
      render_context.line_threads[l] = thread_id;

      // FIXME: handle too-small updates
      // queue is sufficiently filled to fill both bounce buffers, frame can begin
      if (l - min_y == 31) {
#ifdef CONFIG_IDF_TARGET_ESP32S3
        //epd_lcd_frame_prepare_cb(on_frame_done);
        epd_lcd_line_source_cb(&retrieve_line_isr);
        epd_lcd_start_frame();
#else
        xTaskNotifyGive(render_context.feed_tasks[1]);
#endif
      }

      if (l < min_y || l >= max_y || (render_context.drawn_lines != NULL && !render_context.drawn_lines[l - area.y])) {
        uint8_t* buf = NULL;
        while (buf == NULL) buf = lq_current(lq);
        memset(buf, 0x00, lq->element_size);
        lq_commit(lq);
        //if (thread_id) gpio_set_level(15, 1);
        continue;
      }

      uint32_t *lp = (uint32_t *)input_line;
      bool shifted = false;
      const uint8_t *ptr = ptr_start + bytes_per_line * (l - min_y);

#ifdef CONFIG_IDF_TARGET_ESP32S3
      Cache_Start_DCache_Preload((uint32_t)ptr , EPD_WIDTH, 0);
#endif

      if (area.width == EPD_WIDTH && area.x == 0 && !horizontally_cropped && !render_context.error) {
        lp = (uint32_t *)ptr;
      } else if (!render_context.error) {
          assert(false);
        uint8_t *buf_start = (uint8_t *)input_line;
        uint32_t line_bytes = bytes_per_line;

        int min_x = area.x + crop_x;
        if (min_x >= 0) {
          buf_start += min_x / width_divider;
        } else {
          // reduce line_bytes to actually used bytes
          // ptr was already adjusted above
          line_bytes += min_x / width_divider;
        }
        line_bytes = min(line_bytes, EPD_WIDTH / width_divider -
                                         (uint32_t)(buf_start - input_line));

        memcpy(buf_start, ptr, line_bytes);

        int cropped_width = (horizontally_cropped ? crop_w : area.width);
        /// consider half-byte shifts in two-pixel-per-Byte mode.
        if (ppB == 2) {
          // mask last nibble for uneven width
          if (cropped_width % 2 == 1 &&
              min_x / 2 + cropped_width / 2 + 1 < EPD_WIDTH) {
            *(buf_start + line_bytes - 1) |= 0xF0;
          }
          if (area.x % 2 == 1 && !(crop_x % 2 == 1) && min_x < EPD_WIDTH) {
            shifted = true;
            uint32_t remaining =
                (uint32_t)input_line + EPD_WIDTH / 2 - (uint32_t)buf_start;
            uint32_t to_shift = min(line_bytes + 1, remaining);
            // shift one nibble to right
            nibble_shift_buffer_right(buf_start, to_shift);
          }
          // consider bit shifts in bit buffers
        } else if (ppB == 8) {
          // mask last n bits if width is not divisible by 8
          if (cropped_width % 8 != 0 && bytes_per_line + 1 < EPD_WIDTH) {
            uint8_t mask = 0;
            for (int s = 0; s < cropped_width % 8; s++) {
              mask = (mask << 1) | 1;
            }
            *(buf_start + line_bytes - 1) |= ~mask;
          }

          if (min_x % 8 != 0 && min_x < EPD_WIDTH) {
            // shift to right
            shifted = true;
            uint32_t remaining =
                (uint32_t)input_line + EPD_WIDTH / 8 - (uint32_t)buf_start;
            uint32_t to_shift = min(line_bytes + 1, remaining);
            bit_shift_buffer_right(buf_start, to_shift, min_x % 8);
          }
        }
        lp = (uint32_t *)input_line;
      }

      uint8_t* buf = NULL;
      while (buf == NULL) buf = lq_current(lq);

#ifdef CONFIG_IDF_TARGET_ESP32S3
      (*input_calc_func)(lp, buf, render_context.conversion_lut);
      //memcpy(buf, lp, lq->element_size);
// In I2S mode, the other thread is doing the conversion.
#else
      memcpy(buf, lp, lq->element_size);
#endif

      //if (line_start_x > 0 || line_end_x < EPD_WIDTH) {
      //  mask_line_buffer(line_buf, line_start_x, line_end_x);
      //}

      lq_commit(lq);

      if (shifted) {
        memset(input_line, 255, EPD_WIDTH / width_divider);
      }
    }

    xSemaphoreGive(render_context.feed_done_smphr[thread_id]);
  }
}

void IRAM_ATTR i2s_feed_display(int thread_id) {
  uint8_t line_buf[EPD_WIDTH];

  ESP_LOGI("epdiy", "thread id: %d", thread_id);

  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    render_context.skipping = 0;
    EpdRect area = render_context.area;
    enum EpdDrawMode mode = render_context.mode;
    int frame_time = render_context.frame_time;

    lut_func_t input_calc_func = get_lut_function();

    epd_start_frame();
    for (int i = 0; i < FRAME_LINES; i++) {
        LineQueue_t* lq = &render_context.line_queues[0];

        memset(line_buf, 0, EPD_WIDTH);
        while (lq_read(lq, line_buf) < 0) {};

        render_context.lines_consumed += 1;

        if (render_context.drawn_lines != NULL && !render_context.drawn_lines[i - area.y]) {
            i2s_skip_row(frame_time);
            continue;
        }

        (*input_calc_func)(line_buf, epd_get_current_buffer(), render_context.conversion_lut);
        i2s_write_row(frame_time);
    }
    if (!render_context.skipping) {
      // Since we "pipeline" row output, we still have to latch out the last
      // row.
      i2s_write_row(frame_time);
    }
    epd_end_frame();

    xSemaphoreGive(render_context.feed_done_smphr[thread_id]);
    xSemaphoreGive(render_context.frame_done);
  }
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
    for (int i = 0; i < 2; i++) {
      epd_push_pixels(area, white_time, 2);
    }
  }
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
  epd_hw_init(EPD_WIDTH);
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

    //gpio_set_level(15, 1);
  ESP_LOGE("epd", "lut size: %d", lut_size);
  render_context.conversion_lut = (uint8_t *)heap_caps_malloc(lut_size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
  if (render_context.conversion_lut == NULL) {
    ESP_LOGE("epd", "could not allocate LUT!");
    abort();
  }
  render_context.conversion_lut_size = lut_size;

  render_context.frame_done = xSemaphoreCreateBinary();

  for (int i=0; i<NUM_FEED_THREADS; i++) {
      render_context.feed_done_smphr[i] = xSemaphoreCreateBinary();
  }


  //RTOS_ERROR_CHECK(xTaskCreatePinnedToCore((void (*)(void *))provide_out,
  //                                         "epd_fetch", (1 << 12), &fetch_params, 5,
  //                                         NULL, 0));
  int queue_len = 32;
  if (options & EPD_FEED_QUEUE_32) {
    queue_len = 32;
  } else if (options & EPD_FEED_QUEUE_8) {
    queue_len = 8;
  }

#ifdef CONFIG_IDF_TARGET_ESP32S3
  int feed_threads = NUM_FEED_THREADS;
  size_t queue_elem_size = EPD_LINE_BYTES;
  epd_lcd_line_source_cb(NULL);
#else
  size_t queue_elem_size = EPD_WIDTH;
  int feed_threads = 1;
#endif

  for (int i=0; i < feed_threads; i++) {
      render_context.line_queues[i].size = queue_len;
      render_context.line_queues[i].element_size = queue_elem_size;
      render_context.line_queues[i].current = 0;
      render_context.line_queues[i].last = 0;
      render_context.line_queues[i].buf = (uint8_t *)heap_caps_malloc(queue_len * queue_elem_size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
      RTOS_ERROR_CHECK(xTaskCreatePinnedToCore((void (*)(void *))feed_display,
                                               "epd_prep", 1 << 15, (void*)i,
                                               10 | portPRIVILEGE_BIT,
                                               &render_context.feed_tasks[i], i));
      if (render_context.line_queues[i].buf == NULL) {
            ESP_LOGE("epd", "could not allocate line queue!");
            abort();
      }
  }

#ifndef CONFIG_IDF_TARGET_ESP32S3
  render_context.line_queues[1].size = 0;
  render_context.line_queues[1].element_size = 0;
  render_context.line_queues[1].current = 0;
  render_context.line_queues[1].last = 0;
  render_context.line_queues[1].buf = NULL;
  RTOS_ERROR_CHECK(xTaskCreatePinnedToCore((void (*)(void *))i2s_feed_display,
                                           "epd_feed", 1 << 13, (void*)1,
                                           10 | portPRIVILEGE_BIT,
                                           &render_context.feed_tasks[1], 1));
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
