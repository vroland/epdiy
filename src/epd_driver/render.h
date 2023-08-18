#pragma once

#include "epd_driver.h"
/**
 * Initialize the EPD renderer and its render context.
 */
void epd_renderer_init(enum EpdInitOptions options);

/**
 * Deinitialize the EPD renderer and free up its resources.
 */
void epd_renderer_deinit();
