/*
 * Visual tests for framebuffer modes that are not commonly used by the high-level API.
 * Currently, includes 2 pixel per byte (ppB) with static origin color and 8ppB with static origin
 * color.
 */

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <epdiy.h>

#include "sdkconfig.h"

#define WAVEFORM EPD_BUILTIN_WAVEFORM

// choose the default demo board depending on the architecture
#ifdef CONFIG_IDF_TARGET_ESP32
#define DEMO_BOARD epd_board_v6
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define DEMO_BOARD epd_board_v7
#endif

// Singular framebuffer to use for all of the tests.
// Allocated for 2ppB, the least compact that we test here.
uint8_t* framebuffer;
int fb_size;

static inline void checkError(enum EpdDrawError err) {
    if (err != EPD_DRAW_SUCCESS) {
        ESP_LOGE("demo", "draw error: %X", err);
    }
}

/**
 * Clears the screen to white and resets the framebuffer.
 */
void clear() {
    epd_poweron();
    epd_clear();
    epd_poweroff();
    memset(framebuffer, 0xFF, fb_size);
}

void test_8ppB() {
    clear();

    // bytes in a line in 8ppB mode
    int line_bytes = epd_width() / 8;

    int start_line = 100;
    int start_column = 80 / 8;

    // draw differently aligned triangles to check for uniformity
    for (int align = 0; align < 16; align++) {
        for (int height = 0; height < 16; height++) {
            for (int len = 0; len < height; len++) {
                int line = (start_line + 16 * align + height);
                int column = align + len;
                uint8_t* line_address = framebuffer + (line_bytes * line);
                *(line_address + start_column + column / 8) &= ~(1 << (column % 8));
            }
        }
    }

    int black_start_column = 160 / 8;

    // draw a black area for later
    for (int line = 0; line < 200; line++) {
        uint8_t* line_address = framebuffer + (line_bytes * (start_line + line));
        memset(line_address + black_start_column, 0, 32);
    }

    epd_poweron();
    checkError(epd_draw_base(
        epd_full_screen(),
        framebuffer,
        epd_full_screen(),
        MODE_PACKING_8PPB | MODE_DU | PREVIOUSLY_WHITE,
        25,
        NULL,
        NULL,
        &epdiy_ED047TC2
    ));
    epd_poweroff();
}

void app_main() {
    epd_init(&DEMO_BOARD, &ED060XC3, EPD_OPTIONS_DEFAULT);
    // Set VCOM for boards that allow to set this in software (in mV).
    // This will print an error if unsupported. In this case,
    // set VCOM using the hardware potentiometer and delete this line.
    epd_set_vcom(2100);

    fb_size = epd_width() * epd_height() / 2;
    framebuffer = heap_caps_aligned_alloc(16, fb_size, MALLOC_CAP_SPIRAM);

    test_8ppB();

    printf("going to sleep...\n");
    epd_deinit();
    esp_deep_sleep_start();
}