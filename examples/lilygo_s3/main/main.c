/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen */

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "dragon.h"
#include "epd_highlevel.h"
#include "epdiy.h"

EpdiyHighlevelState hl;

// This demo is only for new Lilygo S3 board
#define DEMO_BOARD lilygo_board_s3


void idf_loop() {
    EpdRect dragon_area = { .x = 0, .y = 0, .width = dragon_width, .height = dragon_height };

    int temperature = 25;

    epd_poweron();
    epd_fullclear(&hl, temperature);

    epd_copy_to_framebuffer(dragon_area, dragon_data, epd_hl_get_framebuffer(&hl));

    enum EpdDrawError _err = epd_hl_update_screen(&hl, MODE_GC16, temperature);
    epd_poweroff();
    // 10 secs delay
    vTaskDelay(pdMS_TO_TICKS(10000));
}

void idf_setup() {
    epd_init(&DEMO_BOARD, &ED047TC1, EPD_LUT_64K);
    epd_set_vcom(1560); // No idea what is the best but doesn't matter much for now
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
