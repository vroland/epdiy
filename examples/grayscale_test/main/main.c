/*
 * Test program for displaying a grayscale pattern.
 *
 * Use this to calibrate grayscale timings for new displays or test alternative waveforms.
 */

#include <stdint.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "epd_highlevel.h"
#include "epdiy.h"

// choose the default demo board depending on the architecture
#ifdef CONFIG_IDF_TARGET_ESP32
#define DEMO_BOARD epd_board_v6
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define DEMO_BOARD epd_board_v7
#endif

#define WAVEFORM EPD_BUILTIN_WAVEFORM

EpdiyHighlevelState hl;

void write_grayscale_pattern(bool direction, uint8_t* fb) {
    int ep_width = epd_width();
    uint8_t grayscale_line[ep_width / 2];
    if (direction) {
        for (uint32_t i = 0; i < ep_width / 2; i++) {
            uint8_t segment = i / (ep_width / 16 / 2);
            grayscale_line[i] = (segment << 4) | segment;
        }
    } else {
        for (uint32_t i = 0; i < ep_width / 2; i++) {
            uint8_t segment = (ep_width / 2 - i - 1) / (ep_width / 16 / 2);
            grayscale_line[i] = (segment << 4) | segment;
        }
    }
    for (uint32_t y = 0; y < epd_height(); y++) {
        memcpy(fb + ep_width / 2 * y, grayscale_line, ep_width / 2);
    }
}

void loop() {
    uint8_t* fb = epd_hl_get_framebuffer(&hl);

    write_grayscale_pattern(false, fb);

    int temperature = 25;  // epd_ambient_temperature();

    epd_poweron();
    epd_clear();
    enum EpdDrawError err = epd_hl_update_screen(&hl, MODE_GC16, temperature);
    epd_poweroff();

    vTaskDelay(5000);

    write_grayscale_pattern(true, fb);

    epd_poweron();
    err = epd_hl_update_screen(&hl, MODE_GC16, temperature);
    if (err != EPD_DRAW_SUCCESS) {
        printf("Error in epd_hl_update_screen:%d\n", err);
    }
    epd_poweroff();

    vTaskDelay(100000);
}

void IRAM_ATTR app_main() {
    epd_init(&DEMO_BOARD, &ED097TC2, EPD_LUT_64K);
    epd_set_vcom(1560);
    hl = epd_hl_init(WAVEFORM);

    while (1) {
        loop();
    };
}
