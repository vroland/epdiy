#include "render_context.h"

#include "esp_log.h"

#include "../epdiy.h"
#include "lut.h"
#include "render_method.h"

/// For waveforms without timing and the I2S diving method,
/// the default hold time for each line is 12us
const static int DEFAULT_FRAME_TIME = 120;

static inline int min(int x, int y) { return x < y ? x : y; }

lut_func_t get_lut_function(RenderContext_t* ctx) {
    const enum EpdDrawMode mode = ctx->mode;
    if (mode & MODE_PACKING_2PPB) {
        if (ctx->conversion_lut_size == 1024) {
            if (mode & PREVIOUSLY_WHITE) {
                return &calc_epd_input_4bpp_1k_lut_white;
            } else if (mode & PREVIOUSLY_BLACK) {
                return &calc_epd_input_4bpp_1k_lut_black;
            } else {
                ctx->error |= EPD_DRAW_LOOKUP_NOT_IMPLEMENTED;
            }
        } else if (ctx->conversion_lut_size == (1 << 16)) {
            return &calc_epd_input_4bpp_lut_64k;
        } else {
            ctx->error |= EPD_DRAW_LOOKUP_NOT_IMPLEMENTED;
        }
    } else if (mode & MODE_PACKING_1PPB_DIFFERENCE) {
#ifdef RENDER_METHOD_LCD
        return &calc_epd_input_1ppB_1k_S3_VE;
#endif

        if (ctx->conversion_lut_size == (1 << 16)) {
            return &calc_epd_input_1ppB_64k;
        }
        return NULL;
    } else if (mode & MODE_PACKING_8PPB) {
        return &calc_epd_input_1bpp;
    } else {
        ctx->error |= EPD_DRAW_LOOKUP_NOT_IMPLEMENTED;
    }
    return NULL;
}

void get_buffer_params(RenderContext_t *ctx, int *bytes_per_line, const uint8_t** start_ptr, int* min_y, int* max_y, int* pixels_per_byte) {
    EpdRect area = ctx->area;

    enum EpdDrawMode mode = ctx->mode;
    const EpdRect crop_to = ctx->crop_to;
    const bool horizontally_cropped = !(crop_to.x == 0 && crop_to.width == area.width);
    const bool vertically_cropped = !(crop_to.y == 0 && crop_to.height == area.height);

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

    // calculate start and end row with crop
    *min_y = area.y + crop_y;
    *max_y =
        min(*min_y + (vertically_cropped ? crop_h : area.height), area.height);
    *start_ptr = ptr_start;
    *pixels_per_byte = width_divider;
}

void IRAM_ATTR prepare_context_for_next_frame(RenderContext_t *ctx) {
    int frame_time = DEFAULT_FRAME_TIME;
    if (ctx->phase_times != NULL) {
        frame_time = ctx->phase_times[ctx->current_frame];
    }

    if (ctx->mode & MODE_EPDIY_MONOCHROME) {
        frame_time = MONOCHROME_FRAME_TIME;
    }
    ctx->frame_time = frame_time;

    enum EpdDrawMode mode = ctx->mode;
    const EpdWaveformPhases *phases =
        ctx->waveform->mode_data[ctx->waveform_index]
            ->range_data[ctx->waveform_range];

    ctx->error |=
        calculate_lut(ctx->conversion_lut, ctx->conversion_lut_size, mode,
                      ctx->current_frame, phases);

    ctx->lines_prepared = 0;
    ctx->lines_consumed = 0;
}
