/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen */

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "dragon.h"
#include "epd_highlevel.h"
#include "epdiy.h"

EpdiyHighlevelState hl;

// choose the default demo board depending on the architecture
#ifdef CONFIG_IDF_TARGET_ESP32
#define DEMO_BOARD epd_board_v6
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define DEMO_BOARD epd_board_v7
#endif

void idf_loop() {
    EpdRect dragon_area = {.x = 0, .y = 0, .width = dragon_width, .height = dragon_height};

    int temperature = 25;

    epd_poweron();
    epd_fullclear(&hl, temperature);

    epd_copy_to_framebuffer(dragon_area, dragon_data, epd_hl_get_framebuffer(&hl));

    enum EpdDrawError _err = epd_hl_update_screen(&hl, MODE_GC16, temperature);
    epd_poweroff();

    vTaskDelay(1000);
}

void idf_setup() {
    epd_init(&DEMO_BOARD, &ED097TC2, EPD_LUT_64K);
    epd_set_vcom(1560);
    hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
}

#ifndef ARDUINO_ARCH_ESP32
void app_main() {
    idf_setup();

    while (1) {
        idf_loop();
    };
}
#endif
