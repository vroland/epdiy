#include "render_method.h"

#ifdef RENDER_METHOD_I2S

// output a row to the display.
#include "render.h"
#include <stdint.h>
void IRAM_ATTR i2s_write_row(RenderContext_t *ctx, uint32_t output_time_dus) {
    epd_output_row(output_time_dus);
    ctx->skipping = 0;
}

// skip a display row
void IRAM_ATTR i2s_skip_row(RenderContext_t *ctx,
                            uint8_t pipeline_finish_time) {
    // output previously loaded row, fill buffer with no-ops.
    if (ctx->skipping < 2) {
        memset(epd_get_current_buffer(), 0x00, EPD_LINE_BYTES);
        epd_output_row(pipeline_finish_time);
    } else {
        epd_skip();
    }
    ctx->skipping++;
}

void epd_push_pixels_i2s(EpdRect area, short time, int color) {

    uint8_t row[EPD_LINE_BYTES] = {0};

    const uint8_t color_choice[4] = {DARK_BYTE, CLEAR_BYTE, 0x00, 0xFF};
    for (uint32_t i = 0; i < area.width; i++) {
        uint32_t position = i + area.x % 4;
        uint8_t mask =
            color_choice[color] & (0b00000011 << (2 * (position % 4)));
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

void IRAM_ATTR i2s_feed_display(RenderContext_t *ctx, int thread_id) {
    uint8_t line_buf[EPD_WIDTH];

    ESP_LOGI("epdiy", "thread id: %d", thread_id);

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ctx->skipping = 0;
        EpdRect area = ctx->area;
        enum EpdDrawMode mode = ctx->mode;
        int frame_time = ctx->frame_time;

        lut_func_t input_calc_func = get_lut_function();

        epd_start_frame();
        for (int i = 0; i < FRAME_LINES; i++) {
            LineQueue_t *lq = &ctx->line_queues[0];

            memset(line_buf, 0, EPD_WIDTH);
            while (lq_read(lq, line_buf) < 0) {
            };

            ctx->lines_consumed += 1;

            if (ctx->drawn_lines != NULL && !ctx->drawn_lines[i - area.y]) {
                i2s_skip_row(frame_time);
                continue;
            }

            (*input_calc_func)(line_buf, epd_get_current_buffer(),
                               ctx->conversion_lut);
            i2s_write_row(frame_time);
        }
        if (!ctx->skipping) {
            // Since we "pipeline" row output, we still have to latch out the
            // last row.
            i2s_write_row(frame_time);
        }
        epd_end_frame();

        xSemaphoreGive(ctx->feed_done_smphr[thread_id]);
        xSemaphoreGive(ctx->frame_done);
    }
}

void IRAM_ATTR i2s_feed_frame(RenderContext_t *ctx, int thread_id) {
    uint8_t line_buf[EPD_LINE_BYTES];
    uint8_t input_line[EPD_WIDTH];

    // line must be able to hold 2-pixel-per-byte or 1-pixel-per-byte data
    memset(input_line, 0x00, EPD_WIDTH);

    LineQueue_t *lq = &render_context.line_queues[thread_id];

    EpdRect area = render_context.area;

    lut_func_t input_calc_func = get_lut_function();

    int min_y, max_y, bytes_per_line;
    uint8_t *ptr_start;
    get_buffer_params(ctx, &bytes_per_line, &ptr_start, &min_y, &max_y);


    int l = 0;
    while (l = atomic_fetch_add(&render_context.lines_prepared, 1),
           l < FRAME_LINES) {
        // if (thread_id) gpio_set_level(15, 0);
        render_context.line_threads[l] = thread_id;

        // FIXME: handle too-small updates
        // queue is sufficiently filled to fill both bounce buffers, frame
        // can begin
        if (l - min_y == 31) {
            xTaskNotifyGive(render_context.feed_tasks[1]);
        }

        if (l < min_y || l >= max_y ||
            (render_context.drawn_lines != NULL &&
             !render_context.drawn_lines[l - area.y])) {
            uint8_t *buf = NULL;
            while (buf == NULL)
                buf = lq_current(lq);
            memset(buf, 0x00, lq->element_size);
            lq_commit(lq);
            continue;
        }

        uint32_t *lp = (uint32_t *)input_line;
        bool shifted = false;
        const uint8_t *ptr = ptr_start + bytes_per_line * (l - min_y);

#ifdef CONFIG_IDF_TARGET_ESP32S3
        Cache_Start_DCache_Preload((uint32_t)ptr, EPD_WIDTH, 0);
#endif

        if (area.width == EPD_WIDTH && area.x == 0 && !horizontally_cropped &&
            !render_context.error) {
            lp = (uint32_t *)ptr;
        } else if (!render_context.error) {
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
            line_bytes =
                min(line_bytes, EPD_WIDTH / width_divider -
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
                if (area.x % 2 == 1 && !(crop_x % 2 == 1) &&
                    min_x < EPD_WIDTH) {
                    shifted = true;
                    uint32_t remaining = (uint32_t)input_line + EPD_WIDTH / 2 -
                                         (uint32_t)buf_start;
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
                    uint32_t remaining = (uint32_t)input_line + EPD_WIDTH / 8 -
                                         (uint32_t)buf_start;
                    uint32_t to_shift = min(line_bytes + 1, remaining);
                    bit_shift_buffer_right(buf_start, to_shift, min_x % 8);
                }
            }
            lp = (uint32_t *)input_line;
        }

        uint8_t *buf = NULL;
        while (buf == NULL)
            buf = lq_current(lq);

        memcpy(buf, lp, lq->element_size);

        if (line_start_x > 0 || line_end_x < EPD_WIDTH) {
            mask_line_buffer(line_buf, line_start_x, line_end_x);
        }

        lq_commit(lq);

        if (shifted) {
            memset(input_line, 255, EPD_WIDTH / width_divider);
        }
    }
}

#endif
