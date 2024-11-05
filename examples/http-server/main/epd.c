#include "epd.h"

static EpdiyHighlevelState hl;
static EpdData data;

static inline void checkError(enum EpdDrawError err) {
    if (err != EPD_DRAW_SUCCESS) {
        ESP_LOGE("demo", "draw error: %X", err);
    }
}

EpdData n_epd_data() {
    return data;
}

void n_epd_setup(const EpdDisplay_t* display) {
    epd_init(&epd_board_v7, display, EPD_LUT_64K);
    epd_set_vcom(1560);
    hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    epd_set_rotation(EPD_ROT_LANDSCAPE);
    data.width = epd_rotated_display_width();
    data.height = epd_rotated_display_height();
    data.temperature = epd_ambient_temperature();
}

void n_epd_clear() {
    epd_poweron();
    epd_fullclear(&hl, data.temperature);
    epd_poweroff();
}

void n_epd_draw(uint8_t* content, int x, int y, int width, int height) {
    uint8_t* fb = epd_hl_get_framebuffer(&hl);
    EpdRect area = {
        .x = x,
        .y = y,
        .width = width,
        .height = height,
    };
    epd_draw_rotated_image(area, content, fb);
    epd_poweron();
    checkError(epd_hl_update_screen(&hl, MODE_GC16, data.temperature));
    epd_poweroff();
}
