/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen */

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "dragon.h"
#include "epd_highlevel.h"
#include "epdiy.h"
#include "board/tps65185.h"

EpdiyHighlevelState hl;

// choose the default demo board depending on the architecture
#ifdef CONFIG_IDF_TARGET_ESP32
#define DEMO_BOARD epd_board_v6
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define DEMO_BOARD epd_board_v7
#endif
int temperature = 25;
EpdRect dragon_area = { .x = 0, .y = 0, .width = dragon_width, .height = dragon_height };

void idf_loop() {
    epd_fullclear(&hl, temperature);
    epd_copy_to_framebuffer(dragon_area, dragon_data, epd_hl_get_framebuffer(&hl));
    // Supposedly to measure VCOM the display needs to update
    // all the time with a NULL waveform (That we don't know how it is)
    // MODE_GC16 or MODE_DU ?
    //epd_fill_rect(epd_full_screen(), 0, epd_hl_get_framebuffer(&hl));
    epd_hl_update_screen(&hl, MODE_DU, temperature);

    tps_vcom_kickback_rdy();
    vTaskDelay(20);
    /*
    epd_fill_rect(epd_full_screen(), 255, epd_hl_get_framebuffer(&hl));
    epd_hl_update_screen(&hl, MODE_GC16, temperature);
    vTaskDelay(100); */
}

void idf_setup() {
    epd_init(&DEMO_BOARD, &ED097TC2, EPD_LUT_64K);
    hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
   
    
    tps_vcom_kickback();
    tps_vcom_kickback_start();

    while ( 1 ) {
      idf_loop();
    }
}

#ifndef ARDUINO_ARCH_ESP32
void app_main() {
    idf_setup();
}
#endif
