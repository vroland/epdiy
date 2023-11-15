#include "epd.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

static EpdiyHighlevelState s_state;
uint8_t* g_framebuffer;
static int s_temperature;

// choose the default demo board depending on the architecture
#ifdef CONFIG_IDF_TARGET_ESP32
#define DEMO_BOARD epd_board_v6
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define DEMO_BOARD epd_board_v7
#endif

void initialize_screen(void)
{
    epd_init(&DEMO_BOARD, &ED097TC2, EPD_LUT_64K);
    // Set VCOM for boards that allow to set this in software (in mV).
    // This will print an error if unsupported. In this case,
    // set VCOM using the hardware potentiometer and delete this line.
    epd_set_vcom(1560);

    s_state = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    g_framebuffer = epd_hl_get_framebuffer(&s_state);

    epd_set_rotation(EPD_ROT_PORTRAIT);

    epd_poweron();
    s_temperature = (int)epd_ambient_temperature();
    epd_poweroff();
}

void update_screen(void)
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

void clear_screen(void)
{
    epd_hl_set_all_white(&s_state);
    update_screen();
}

void full_clear_screen(void)
{
    epd_poweron();
    epd_fullclear(&s_state, s_temperature);
    epd_poweroff();
}
