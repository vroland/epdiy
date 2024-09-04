#pragma once

#include <stdint.h>
#include "epdiy.h"

// Make a block of 4 pixels lighter on the EPD.
#define CLEAR_BYTE 0B10101010
// Make a block of 4 pixels darker on the EPD.
#define DARK_BYTE 0B01010101

/**
 * Type signature of a framebuffer to display output lookup function.
 */
typedef void (*lut_func_t)(
    const uint32_t* line_buffer, uint8_t* epd_input, const uint8_t* lut, uint32_t epd_width
);

/**
 * Type signature of a LUT preparation function.
 */
typedef void (*lut_build_func_t)(uint8_t* lut, const EpdWaveformPhases* phases, int frame);

typedef struct {
    lut_build_func_t build_func;
    lut_func_t lookup_func;
} LutFunctionPair;

/**
 * Select the appropriate LUT building and lookup function
 * for the selected draw mode and allocated LUT size.
 */
LutFunctionPair find_lut_functions(enum EpdDrawMode mode, uint32_t lut_size);
