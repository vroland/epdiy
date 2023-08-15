#include "render.h"
#include "display_ops.h"
#include "include/epd_driver.h"


void i2s_feed_frame(RenderContext_t *ctx, int thread_id);
void i2s_output_frame(RenderContext_t *ctx, int thread_id);
void i2s_do_update(RenderContext_t *ctx);

void epd_push_pixels_i2s(RenderContext_t *ctx, EpdRect area, short time, int color);
