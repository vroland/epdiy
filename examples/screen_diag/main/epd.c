#include "epd.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

static EpdiyHighlevelState s_state;
uint8_t* g_framebuffer;
static int s_temperature;

void initialize_screen(void)
{
    epd_init(EPD_OPTIONS_DEFAULT);
    s_state = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    g_framebuffer = epd_hl_get_framebuffer(&s_state);

    epd_set_rotation(EPD_ROT_PORTRAIT);

    s_temperature = (int)epd_ambient_temperature();

    clear_full_screen();
}

void update_full_screen(void)
{
    enum EpdDrawError err;

    epd_poweron();
    err = epd_hl_update_screen(&s_state, EPD_MODE_DEFAULT, s_temperature);
    taskYIELD();
    epd_poweroff();

    if (err != EPD_DRAW_SUCCESS) {
        ESP_LOGW("screen_diag", "Could not update screen. Reason: %d", err);
    }
}

void clear_full_screen(void)
{
    epd_poweron();
    epd_fullclear(&s_state, s_temperature);
    epd_poweroff();
}
