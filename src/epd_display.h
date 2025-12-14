#pragma once

#include <stdint.h>
#include "epd_internals.h"

/**
 * Display type as "compatibility classes",
 * Grouping displays by workarounds needed.
 */
enum EpdDisplayType {
    /// A generic EPD, assume default config.
    DISPLAY_TYPE_GENERIC,
    /// Fast display where we can get away with low hold times.
    DISPLAY_TYPE_ED097TC2,
    /// Display with horizontal mirroring.
    DISPLAY_TYPE_HORIZONTAL_MIRRORED,
};

/**
 * Display Color filter type
 * This applies only to those epapers with CFA on top
 */
enum EpdColorFilterType {
    /// No color filter, the default for all grayscale epapers
    DISPLAY_CFA_NONE,
    /// Eink.com Kaleido
    DISPLAY_CFA_KALEIDO,
    /// https://en.wf-tech.com called DES color and sold by Good-Display
    DISPLAY_CFA_DES,
};

typedef struct {
    /// Width of the display in pixels.
    int width;
    /// Height of the display in pixels.
    int height;

    /// Width of the data bus in bits.
    uint8_t bus_width;
    /// Speed of the data bus in MHz, if configurable.
    /// (Only used by the LCD based renderer in V7+)
    int bus_speed;

    /// Default waveform to use.
    const EpdWaveform* default_waveform;
    /// Display type
    enum EpdDisplayType display_type;
    /// Display Color filter type (CFA)
    enum EpdColorFilterType display_color_filter;
} EpdDisplay_t;

extern const EpdDisplay_t ED060SCT;
extern const EpdDisplay_t ED060XC3;
extern const EpdDisplay_t ED097OC4;
extern const EpdDisplay_t ED097TC2;
extern const EpdDisplay_t ED133UT2;
extern const EpdDisplay_t ED047TC1;
extern const EpdDisplay_t ED047TC2;
extern const EpdDisplay_t ED078KC1;
extern const EpdDisplay_t ED052TC4;
extern const EpdDisplay_t EC060KC1;
extern const EpdDisplay_t EC060KH5;