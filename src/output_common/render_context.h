#pragma once

#include <stdatomic.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "../epdiy.h"
#include "line_queue.h"

#define NUM_RENDER_THREADS 2

typedef struct {
    EpdRect area;
    EpdRect crop_to;
    const bool *drawn_lines;
    const uint8_t *data_ptr;

    /// The display width for quick access.
    int display_width;
    /// The display height for quick access.
    int display_height;

    /// index of the next line of data to process
    atomic_int lines_prepared;
    volatile int lines_consumed;
    int lines_total;

    /// frame currently in the current update cycle
    int current_frame;
    /// number of frames in the current update cycle
    int cycle_frames;

    TaskHandle_t feed_tasks[NUM_RENDER_THREADS];
    SemaphoreHandle_t feed_done_smphr[NUM_RENDER_THREADS];
    SemaphoreHandle_t frame_done;
    /// Line buffers for feed tasks
    uint8_t* feed_line_buffers[NUM_RENDER_THREADS];

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
    LineQueue_t line_queues[NUM_RENDER_THREADS];
    uint8_t* line_threads;

    /// track line skipping when working in old i2s mode
    int skipping;
} RenderContext_t;

typedef void (*lut_func_t)(const uint32_t *, uint8_t *, const uint8_t *, uint32_t);

/**
 * Depending on the render context, decide which LUT function to use.
 * If the lookup fails, an error flag in the context is set.
 */
lut_func_t get_lut_function(RenderContext_t* ctx);

/**
 * Based on the render context, assign the bytes per line,
 * framebuffer start pointer, min and max vertical positions and the pixels per byte.
 */
void get_buffer_params(
    RenderContext_t *ctx,
    int *bytes_per_line,
    const uint8_t** start_ptr,
    int* min_y,
    int* max_y,
    int* pixels_per_byte
);

/**
 * Prepare the render context for drawing the next frame.
 *
 * (Reset counters, etc)
 */
void prepare_context_for_next_frame(RenderContext_t *ctx);
