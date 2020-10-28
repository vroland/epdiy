/* Simple helper program enabling EPD power to allow for easier VCOM calibration. */

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>
#include "esp32/himem.h"

#include "epd_driver.h"

uint8_t *img_buf;

void delay(uint32_t millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }

uint32_t millis() { return esp_timer_get_time() / 1000; }

uint8_t* grayscale_img;
uint8_t* grayscale_img2;

int min(int a, int b) {
    return a < b ? a : b;
}

void epd_task() {
  epd_init();
  heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
  heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
  ESP_LOGI("main", "waiting for one second before poweron...");
  vTaskDelay(1000);
  ESP_LOGI("main", "enabling VCOMM...");
  epd_poweron();
  ESP_LOGI("main", "VCOMM enabled.");
  while (1) {
    vTaskDelay(1000);
  };
}

void app_main() {
  xTaskCreate(&epd_task, "epd task", 10000, NULL, 2, NULL);
}
