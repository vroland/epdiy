/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen */

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
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

void draw_dragon() {
    EpdRect dragon_area = { .x = 0, .y = 0, .width = dragon_width, .height = dragon_height };
    epd_poweron();
    epd_fullclear(&hl, temperature);
    epd_copy_to_framebuffer(dragon_area, dragon_data, epd_hl_get_framebuffer(&hl));
    enum EpdDrawError _err = epd_hl_update_screen(&hl, MODE_GC16, temperature);
    epd_poweroff();
}

void idf_loop() {
    // make a full black | white print to force epdiy to send the update
    epd_fill_rect(epd_full_screen(), 0, epd_hl_get_framebuffer(&hl));
    epd_hl_update_screen(&hl, MODE_DU, temperature);
    vTaskDelay(pdMS_TO_TICKS(1));
    epd_fill_rect(epd_full_screen(), 255, epd_hl_get_framebuffer(&hl));
    epd_hl_update_screen(&hl, MODE_DU, temperature);
}

void idf_setup() {
    epd_init(&DEMO_BOARD, &ED097TC2, EPD_LUT_64K);
    hl = epd_hl_init(&epdiy_NULL);
    // starts the board in kickback more
    tps_vcom_kickback();

    // display starts to pass BLACK to WHITE but doing nothing+
    // dince the NULL waveform is full of 0 "Do nothing for each pixel"
    idf_loop();
    // start measure and set ACQ bit:
    tps_vcom_kickback_start();
    int isrdy = 1;
    int kickback_volt = 0;
    while (kickback_volt == 0) {
        idf_loop();
        isrdy++;
        kickback_volt = tps_vcom_kickback_rdy();
    }
    ESP_LOGI("VCOM kick-back", "got value of %d mV. It was ready in %d refreshes", kickback_volt, isrdy);

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

#ifndef ARDUINO_ARCH_ESP32
void app_main() {
    idf_setup();
}
#endif
