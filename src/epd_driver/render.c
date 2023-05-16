#include "epd_temperature.h"

#include "render_method.h"
#include "display_ops.h"
#include "epd_driver.h"
#include "include/epd_board.h"
#include "include/epd_driver.h"
#include "include/epd_internals.h"
#include "lut.h"
#include "render.h"
#include "s3_lcd.h"
#include "esp_log.h"
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "xtensa/core-macros.h"
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

#ifdef RENDER_METHOD_LCD
#include "render_lcd.h"
#elif defined(RENDER_METHOD_I2S)
#include "render_i2s.h"
#else
#error "invalid render method!"
#endif

inline int min(int x, int y) { return x < y ? x : y; }
inline int max(int x, int y) { return x > y ? x : y; }

const int clear_cycle_time = 12;

#define RTOS_ERROR_CHECK(x)                                                    \
    do {                                                                       \
        esp_err_t __err_rc = (x);                                              \
        if (__err_rc != pdPASS) {                                              \
            abort();                                                           \
        }                                                                      \
    } while (0)

static RenderContext_t render_context;

void epd_push_pixels(EpdRect area, short time, int color) {
    render_context.area = area;
#ifdef RENDER_METHOD_LCD
    epd_push_pixels_lcd(&render_context, time, color);
#else
    epd_push_pixels_i2s(area, time, color);
#endif
}

///////////////////////////// Coordination ///////////////////////////////

/**
 * Find the waveform temperature range index for a given temperature in °C.
 * If no range in the waveform data fits the given temperature, return the
 * closest one.
 * Returns -1 if the waveform does not contain any temperature range.
 */
int waveform_temp_range_index(const EpdWaveform *waveform, int temperature) {
    int idx = 0;
    if (waveform->num_temp_ranges == 0) {
        return -1;
    }
    while (idx < waveform->num_temp_ranges - 1 &&
           waveform->temp_intervals[idx].min < temperature) {
        idx++;
    }
    return idx;
}

static int get_waveform_index(const EpdWaveform *waveform,
                              enum EpdDrawMode mode) {
    for (int i = 0; i < waveform->num_modes; i++) {
        if (waveform->mode_data[i]->type == (mode & 0x3F)) {
            return i;
        }
    }
    return -1;
}

/////////////////////////////  API Procedures //////////////////////////////////

// FIXME: fix misleading naming:
//  area -> buffer dimensions
//  crop -> area taken out of buffer
enum EpdDrawError IRAM_ATTR epd_draw_base(
    EpdRect area, const uint8_t *data, EpdRect crop_to, enum EpdDrawMode mode,
    int temperature, const bool *drawn_lines, const EpdWaveform *waveform) {
    int waveform_range = waveform_temp_range_index(waveform, temperature);
    if (waveform_range < 0) {
        return EPD_DRAW_NO_PHASES_AVAILABLE;
    }
    int waveform_index = 0;
    uint8_t frame_count = 0;
    const EpdWaveformPhases *waveform_phases = NULL;

    // no waveform required for monochrome mode
    if (!(mode & MODE_EPDIY_MONOCHROME)) {
        waveform_index = get_waveform_index(waveform, mode);
        if (waveform_index < 0) {
            return EPD_DRAW_MODE_NOT_FOUND;
        }

        waveform_phases =
            waveform->mode_data[waveform_index]->range_data[waveform_range];
        // FIXME: error if not present
        frame_count = waveform_phases->phases;
    } else {
        frame_count = 1;
    }

    if (crop_to.width < 0 || crop_to.height < 0) {
        return EPD_DRAW_INVALID_CROP;
    }

    const bool crop = (crop_to.width > 0 && crop_to.height > 0);
    if (crop && (crop_to.width > area.width || crop_to.height > area.height ||
                 crop_to.x > area.width || crop_to.y > area.height)) {
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

    lcd_draw_prepared(&render_context);

    if (render_context.error != EPD_DRAW_SUCCESS) {
        return render_context.error;
    }
    return EPD_DRAW_SUCCESS;
}



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

void get_buffer_params(RenderContext_t *ctx, int *bytes_per_line, const uint8_t** start_ptr, int* min_y, int* max_y) {
    EpdRect area = ctx->area;

    enum EpdDrawMode mode = ctx->mode;
    const EpdRect crop_to = ctx->crop_to;
    const bool horizontally_cropped =
        !(crop_to.x == 0 && crop_to.width == area.width);
    const bool vertically_cropped =
        !(crop_to.y == 0 && crop_to.height == area.height);

    // number of pixels per byte of input data
    int width_divider = 0;

    if (mode & MODE_PACKING_1PPB_DIFFERENCE) {
        *bytes_per_line = area.width;
        width_divider = 1;
    } else if (mode & MODE_PACKING_2PPB) {
        *bytes_per_line = area.width / 2 + area.width % 2;
        width_divider = 2;
    } else if (mode & MODE_PACKING_8PPB) {
        *bytes_per_line = (area.width / 8 + (area.width % 8 > 0));
        width_divider = 8;
    } else {
        ctx->error |= EPD_DRAW_INVALID_PACKING_MODE;
    }

    int crop_x = (horizontally_cropped ? crop_to.x : 0);
    int crop_w = (horizontally_cropped ? crop_to.width : 0);
    int crop_y = (vertically_cropped ? crop_to.y : 0);
    int crop_h = (vertically_cropped ? crop_to.height : 0);

    const uint8_t *ptr_start = ctx->data_ptr;

    // Adjust for negative starting coordinates with optional crop
    if (area.x - crop_x < 0) {
        ptr_start += -(area.x - crop_x) / width_divider;
    }

    if (area.y - crop_y < 0) {
        ptr_start += -(area.y - crop_y) * *bytes_per_line;
    }

    // interval of the output line that is needed
    // FIXME: only lookup needed parts
    int line_start_x = area.x + (horizontally_cropped ? crop_to.x : 0);
    int line_end_x =
        line_start_x + (horizontally_cropped ? crop_to.width : area.width);
    line_start_x = min(max(line_start_x, 0), EPD_WIDTH);
    line_end_x = min(max(line_end_x, 0), EPD_WIDTH);

    // calculate start and end row with crop
    *min_y = area.y + crop_y;
    *max_y =
        min(*min_y + (vertically_cropped ? crop_h : area.height), area.height);
    *start_ptr = ptr_start;
}

void IRAM_ATTR feed_display(int thread_id) {
    ESP_LOGI("epdiy", "thread id: %d", thread_id);

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // xSemaphoreTake(render_context.frame_start_smphr[thread_id],
        // portMAX_DELAY);

        lcd_feed_frame(&render_context, thread_id);

        xSemaphoreGive(render_context.feed_done_smphr[thread_id]);
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
    // Either the board should be set in menuconfig or the epd_set_board() must
    // be called before epd_init()
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

    // gpio_set_level(15, 1);
    ESP_LOGE("epd", "lut size: %d", lut_size);
    render_context.conversion_lut = (uint8_t *)heap_caps_malloc(
        lut_size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (render_context.conversion_lut == NULL) {
        ESP_LOGE("epd", "could not allocate LUT!");
        abort();
    }
    render_context.conversion_lut_size = lut_size;

    render_context.frame_done = xSemaphoreCreateBinary();

    for (int i = 0; i < NUM_FEED_THREADS; i++) {
        render_context.feed_done_smphr[i] = xSemaphoreCreateBinary();
    }

    // RTOS_ERROR_CHECK(xTaskCreatePinnedToCore((void (*)(void *))provide_out,
    //                                          "epd_fetch", (1 << 12),
    //                                          &fetch_params, 5, NULL, 0));
    int queue_len = 32;
    if (options & EPD_FEED_QUEUE_32) {
        queue_len = 32;
    } else if (options & EPD_FEED_QUEUE_8) {
        queue_len = 8;
    }

#ifdef RENDER_METHOD_LCD
    int feed_threads = NUM_FEED_THREADS;
    size_t queue_elem_size = EPD_LINE_BYTES;
    epd_lcd_line_source_cb(NULL, NULL);
#else
    size_t queue_elem_size = EPD_WIDTH;
    int feed_threads = 1;
#endif

    for (int i = 0; i < feed_threads; i++) {
        render_context.line_queues[i].size = queue_len;
        render_context.line_queues[i].element_size = queue_elem_size;
        render_context.line_queues[i].current = 0;
        render_context.line_queues[i].last = 0;
        render_context.line_queues[i].buf = (uint8_t *)heap_caps_malloc(
            queue_len * queue_elem_size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        RTOS_ERROR_CHECK(xTaskCreatePinnedToCore(
            (void (*)(void *))feed_display, "epd_prep", 1 << 15, (void *)i,
            10 | portPRIVILEGE_BIT, &render_context.feed_tasks[i], i));
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
    RTOS_ERROR_CHECK(xTaskCreatePinnedToCore(
        (void (*)(void *))i2s_feed_display, "epd_feed", 1 << 13, (void *)1,
        10 | portPRIVILEGE_BIT, &render_context.feed_tasks[1], 1));
#endif
}

EpdRect epd_difference_image_base(const uint8_t *to, const uint8_t *from,
                                  EpdRect crop_to, int fb_width, int fb_height,
                                  uint8_t *interlaced, bool *dirty_lines,
                                  uint8_t *from_or, uint8_t *from_and) {
    assert(from_or != NULL);
    assert(from_and != NULL);
    // OR over all pixels of the "from"-image
    *from_or = 0x00;
    // AND over all pixels of the "from"-image
    *from_and = 0x0F;

    uint8_t dirty_cols[EPD_WIDTH] = {0};
    int x_end = min(fb_width, crop_to.x + crop_to.width);
    int y_end = min(fb_height, crop_to.y + crop_to.height);

    for (int y = crop_to.y; y < y_end; y++) {
        uint8_t dirty = 0;
        for (int x = crop_to.x; x < x_end; x++) {
            uint8_t t = *(to + y * fb_width / 2 + x / 2);
            t = (x % 2) ? (t >> 4) : (t & 0x0f);
            uint8_t f = *(from + y * fb_width / 2 + x / 2);
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
        if (dirty_cols[min_x] != 0)
            break;
    }
    for (max_x = x_end - 1; max_x >= crop_to.x; max_x--) {
        if (dirty_cols[max_x] != 0)
            break;
    }
    for (min_y = crop_to.y; min_y < y_end; min_y++) {
        if (dirty_lines[min_y] != 0)
            break;
    }
    for (max_y = y_end - 1; max_y >= crop_to.y; max_y--) {
        if (dirty_lines[max_y] != 0)
            break;
    }
    EpdRect crop_rect = {
        .x = min_x,
        .y = min_y,
        .width = max(max_x - min_x + 1, 0),
        .height = max(max_y - min_y + 1, 0),
    };
    return crop_rect;
}

EpdRect epd_difference_image(const uint8_t *to, const uint8_t *from,
                             uint8_t *interlaced, bool *dirty_lines) {
    uint8_t from_or = 0;
    uint8_t from_and = 0;
    return epd_difference_image_base(to, from, epd_full_screen(), EPD_WIDTH,
                                     EPD_HEIGHT, interlaced, dirty_lines,
                                     &from_or, &from_and);
}

EpdRect epd_difference_image_cropped(const uint8_t *to, const uint8_t *from,
                                     EpdRect crop_to, uint8_t *interlaced,
                                     bool *dirty_lines, bool *previously_white,
                                     bool *previously_black) {

    uint8_t from_or, from_and;

    EpdRect result =
        epd_difference_image_base(to, from, crop_to, EPD_WIDTH, EPD_HEIGHT,
                                  interlaced, dirty_lines, &from_or, &from_and);

    if (previously_white != NULL)
        *previously_white = (from_and == 0x0F);
    if (previously_black != NULL)
        *previously_black = (from_or == 0x00);
    return result;
}
