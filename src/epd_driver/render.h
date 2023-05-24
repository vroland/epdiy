#ifndef __RENDER_H
#define __RENDER_H

#include <stdatomic.h>
#include "line_queue.h"
#include "include/epd_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define CLEAR_BYTE 0B10101010
#define DARK_BYTE 0B01010101

#define NUM_FEED_THREADS 2

// When using the LCD peripheral, we may need padding lines to
// satisfy the bounce buffer size requirements
#ifndef CONFIG_EPD_BOARD_S3_PROTOTYPE
#define FRAME_LINES EPD_HEIGHT
#else
#define FRAME_LINES (((EPD_HEIGHT + 7) / 8) * 8)
#endif

const static int DEFAULT_FRAME_TIME = 120;

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

typedef void (*lut_func_t)(const uint32_t *, uint8_t *, const uint8_t *, uint32_t);

void get_buffer_params(RenderContext_t *ctx, int *bytes_per_line, const uint8_t** start_ptr, int* min_y, int* max_y);
lut_func_t get_lut_function();

#endif // __RENDER_H
