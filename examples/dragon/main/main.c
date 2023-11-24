/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen */

#include <string.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "dragon.h"
#include "epd_highlevel.h"
#include "epdiy.h"
#include "output_common/lut.h"
#include "ED097TC2.h"

EpdiyHighlevelState hl;

// choose the default demo board depending on the architecture
#ifdef CONFIG_IDF_TARGET_ESP32
#define DEMO_BOARD epd_board_v6
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define DEMO_BOARD epd_board_v7
#endif

#include "esp_timer.h"
#include "esp_random.h"

void IRAM_ATTR calc_epd_input_1ppB_64k(const uint32_t *ld, uint8_t *epd_input, const uint8_t *conversion_lut, uint32_t epd_width);

esp_err_t calc_epd_input_1ppB_64k_ve(const uint32_t *ld, uint8_t *epd_input, const uint8_t *conversion_lut, uint32_t epd_width);

enum EpdDrawError calculate_lut(
    uint8_t* lut,
    int lut_size,
    enum EpdDrawMode mode,
    int frame,
    const EpdWaveformPhases* phases
);

void IRAM_ATTR benchmark_lut_calculation(void) {
    const unsigned MEASUREMENTS = 1000;

    // fill a random benchmark line
    static uint32_t line[468] __attribute__((aligned(16)));

    esp_fill_random(line, 1872);

    uint8_t* lut = heap_caps_malloc(1 << 16, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    calculate_lut(lut, 1 << 16, MODE_GC16 | MODE_PACKING_1PPB_DIFFERENCE, 5, ed097tc2.mode_data[MODE_GC16]->range_data[0]);


    uint32_t* lut_1k = heap_caps_malloc(1 << 10, MALLOC_CAP_32BIT | MALLOC_CAP_INTERNAL);
    for (int i=0; i<256; i++) {
        lut_1k[i] = lut[i] & 0x3;
    }

    line[0] = 0x0F000000;
    line[1] = 0x01020304;
    line[2] = 0xB33348D3;
    line[3] = 0x38E2C376;

    volatile uint8_t target_buf[468];
    uint8_t ground_truth[468];

    int64_t start = esp_timer_get_time();

    for (int retries = 0; retries < MEASUREMENTS; retries++) {
        calc_epd_input_1ppB_64k(line, target_buf, lut, 1872);
    }

    int64_t end = esp_timer_get_time();

    memcpy(ground_truth, target_buf, 468);
    memset(target_buf, 0, 468);

    printf("plain: %u iterations took %llu milliseconds (%llu microseconds per invocation)\n",
           MEASUREMENTS, (end - start)/1000, (end - start)/MEASUREMENTS);

    start = esp_timer_get_time();

    for (int retries = 0; retries < MEASUREMENTS; retries++) {
        memcpy(target_buf, line, 468);
        memcpy(target_buf, line, 468);
        memcpy(target_buf, line, 468);
        memcpy(target_buf, line, 468);
    }

    end = esp_timer_get_time();

    printf("memcpy: %u iterations took %llu milliseconds (%llu microseconds per invocation)\n",
           MEASUREMENTS, (end - start)/1000, (end - start)/MEASUREMENTS);

    memset(target_buf, 0, 468);

    start = esp_timer_get_time();

    for (int retries = 0; retries < MEASUREMENTS; retries++) {
        calc_epd_input_1ppB_64k_ve(line, target_buf, lut_1k, 1872);
    }

    end = esp_timer_get_time();

    printf("optimized: %u iterations took %f milliseconds (%llu microseconds per invocation)\n",
           MEASUREMENTS, (end - start)/1000.0, (end - start)/MEASUREMENTS);

    if (memcmp(ground_truth, target_buf, 468) != 0) {
        printf("COMPARE FAILED! \n");
        for (int i=0; i<30; i++) {
            printf("%lX     %X %X\n", line[i], ground_truth[i], target_buf[i]);
        }
    }

    free(lut);

    vTaskDelay(10);
}

void idf_loop() {
    benchmark_lut_calculation();
 //   EpdRect dragon_area = {.x = 0, .y = 0, .width = dragon_width, .height = dragon_height};

 //   int temperature = 25;

 //   epd_poweron();
 //   epd_fullclear(&hl, temperature);

 //   epd_copy_to_framebuffer(dragon_area, dragon_data, epd_hl_get_framebuffer(&hl));

 //   enum EpdDrawError _err = epd_hl_update_screen(&hl, MODE_GC16, temperature);
 //   epd_poweroff();

 //   vTaskDelay(1000);
}//

void idf_setup() {
    // epd_init(&DEMO_BOARD, &ED097TC2, EPD_LUT_64K);
    // epd_set_vcom(1560);
    // hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
}

#ifndef ARDUINO_ARCH_ESP32
void app_main() {
    idf_setup();

    while (1) {
        idf_loop();
    };
}
#endif
