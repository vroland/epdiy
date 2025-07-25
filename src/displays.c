#include "epd_display.h"

const EpdDisplay_t ED060SCT = {
    .width = 800,
    .height = 600,
    .bus_width = 8,
    .bus_speed = 20,
    .default_waveform = &epdiy_ED060SCT,
    .display_type = DISPLAY_TYPE_GENERIC,
    .display_color_filter = DISPLAY_CFA_NONE
};

const EpdDisplay_t ED060XC3 = {
    .width = 1024,
    .height = 768,
    .bus_width = 8,
    .bus_speed = 20,
    .default_waveform = &epdiy_ED060XC3,
    .display_type = DISPLAY_TYPE_GENERIC,
    .display_color_filter = DISPLAY_CFA_NONE
};

const EpdDisplay_t ED097OC4 = {
    .width = 1200,
    .height = 825,
    .bus_width = 8,
    .bus_speed = 15,
    .default_waveform = &epdiy_ED097OC4,
    .display_type = DISPLAY_TYPE_GENERIC,
    .display_color_filter = DISPLAY_CFA_NONE
};

const EpdDisplay_t ED097TC2 = {
    .width = 1200,
    .height = 825,
    .bus_width = 8,
    .bus_speed = 12, // 22 works
    .default_waveform = &epdiy_ED097TC2,
    .display_type = DISPLAY_TYPE_ED097TC2,
    .display_color_filter = DISPLAY_CFA_NONE
};

const EpdDisplay_t ED133UT2 = {
    .width = 1600,
    .height = 1200,
    .bus_width = 8,
    .bus_speed = 16,
    .default_waveform = &epdiy_ED133UT2,
    .display_type = DISPLAY_TYPE_ED097TC2,
    .display_color_filter = DISPLAY_CFA_NONE
};

const EpdDisplay_t ED047TC1 = {
    .width = 960,
    .height = 540,
    .bus_width = 8,
    .bus_speed = 20,
    .default_waveform = &epdiy_ED047TC1,
    .display_type = DISPLAY_TYPE_GENERIC,
    .display_color_filter = DISPLAY_CFA_NONE
};

const EpdDisplay_t ED047TC2 = {
    .width = 960,
    .height = 540,
    .bus_width = 8,
    .bus_speed = 20,
    .default_waveform = &epdiy_ED047TC2,
    .display_type = DISPLAY_TYPE_GENERIC,
    .display_color_filter = DISPLAY_CFA_NONE
};

const EpdDisplay_t ED078KC1 = {
    .width = 1872,
    .height = 1404,
    .bus_width = 16,
    .bus_speed = 10,
    .default_waveform = &epdiy_ED097TC2,
    .display_type = DISPLAY_TYPE_GENERIC,
    .display_color_filter = DISPLAY_CFA_NONE
};


// This display is produced by https://en.wf-tech.com and sold by Good-Display
const EpdDisplay_t GDEW101C01 = {
    .width = 2232,
    .height = 1680,
    .bus_width = 16,
    .bus_speed = 10,
    .default_waveform = &epdiy_ED097TC2,
    .display_type = DISPLAY_TYPE_GENERIC,
    .display_color_filter = DISPLAY_CFA_DES
};
// EINK Kaleido display
// epdiy_ED047TC2 is the one that gets more grays, but is not right
const EpdDisplay_t EC060KH5 = {
    .width = 1448,
    .height = 1072,
    .bus_width = 8,
    .bus_speed = 20,
    .default_waveform = &epdiy_ED097TC2,
    .display_type = DISPLAY_TYPE_GENERIC,
    .display_color_filter = DISPLAY_CFA_KALEIDO
};

// Attention is by default horizontal rows mirrored
const EpdDisplay_t ED052TC4 = {
    .width = 1280,
    .height = 720,
    .bus_width = 8,
    .bus_speed = 22,
    .default_waveform = &epdiy_ED097TC2,
    .display_type = DISPLAY_TYPE_ED097TC2,
};