/*
 * Simple helper program enabling EPD power to allow for easier VCOM calibration.
 *
 * This is only needed for boards V5 or lower!
 **/

#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "epdiy.h"

// choose the default demo board depending on the architecture
#ifdef CONFIG_IDF_TARGET_ESP32
#define DEMO_BOARD epd_board_v6
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define DEMO_BOARD epd_board_v7
#endif

void enable_vcom() {
    epd_init(&DEMO_BOARD, &ED097TC2, EPD_LUT_64K);
    ESP_LOGI("main", "waiting for one second before poweron...");
    vTaskDelay(1000);
    ESP_LOGI("main", "enabling VCOMM...");
    epd_poweron();
    ESP_LOGI(
        "main",
        "VCOMM enabled. You can now adjust the on-board trimmer to the VCOM value specified on the "
        "display."
    );
    while (1) {
        vTaskDelay(1000);
    };
}

void app_main() {
    enable_vcom();
}
