#include "render.h"

void epd_push_pixels_lcd(RenderContext_t *ctx, short time, int color);
void lcd_do_update(RenderContext_t *ctx);
void lcd_feed_frame(RenderContext_t *ctx, int thread_id);
