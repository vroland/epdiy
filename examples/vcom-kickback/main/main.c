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
    //vTaskDelay(pdMS_TO_TICKS(1));
    //epd_fill_rect(epd_full_screen(), 0, epd_hl_get_framebuffer(&hl));
    epd_hl_update_screen(&hl, MODE_DU, temperature);
}

void idf_setup() {
    epd_init(&DEMO_BOARD, &ED097TC2, EPD_LUT_64K);
    hl = epd_hl_init(&epdiy_NULL);
    tps_vcom_kickback();

    for (int a = 5; a>0; a--) {
        idf_loop();
    }
    tps_vcom_kickback_start();
    
    int isrdy = 1;
    int kickback_volt = 0;
    while (kickback_volt == 0) {
    idf_loop();
    isrdy++;
    kickback_volt = tps_vcom_kickback_rdy();
    }
    printf("VCOM reading of %d mV. was ready in %d refreshes\n", kickback_volt, isrdy);

}

#ifndef ARDUINO_ARCH_ESP32
void app_main() {
    idf_setup();
}
#endif
