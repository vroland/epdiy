/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen.
 *
 * Write an image into a header file using a 3...2...1...0 format per pixel,
 * for 4 bits color (16 colors - well, greys.) MSB first.  At 80 MHz, screen
 * clears execute in 1.075 seconds and images are drawn in 1.531 seconds.
 */

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

uint8_t* base_image;
uint8_t* grayscale_img;
uint8_t* grayscale_img2;

int min(int a, int b) {
    return a < b ? a : b;
}

void loop() {
  uint32_t t1, t2;
  ESP_LOGI("epd_task", "WHITE_ON_BLACK drawing:");
  epd_poweron();
  epd_clear();
  t1 = esp_timer_get_time();
  epd_push_pixels(epd_full_screen(), 20, 0);
  epd_push_pixels(epd_full_screen(), 20, 0);
  epd_push_pixels(epd_full_screen(), 20, 0);
  epd_draw_image(epd_full_screen(), grayscale_img2, WHITE_ON_BLACK);
  t2 = esp_timer_get_time();
  printf("draw took %dms.\n", (t2 - t1) / 1000);
  epd_poweroff();

  vTaskDelay(3000);

  ESP_LOGI("epd_task", "BLACK_ON_WHITE drawing:");
  epd_poweron();
  epd_clear();
  t1 = esp_timer_get_time();
  epd_draw_image(epd_full_screen(), grayscale_img, BLACK_ON_WHITE);
  t2 = esp_timer_get_time();
  printf("draw took %dms.\n", (t2 - t1) / 1000);
  epd_poweroff();
  vTaskDelay(3000);
}

void epd_task() {
  epd_init();
  heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
  heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
  size_t memcnt = esp_himem_get_phys_size();
  size_t memfree = esp_himem_get_free_size();
  printf("himem phys: %d, free: %d\n", memcnt, memfree);
  while (1) {
    loop();
  };
}

void app_main() {


  // copy the image data to SRAM for faster display time
  grayscale_img = (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT / 2, MALLOC_CAP_SPIRAM);
  if (grayscale_img == NULL) {
     ESP_LOGE("epd_task", "Could not allocate framebuffer in PSRAM!");
  }

  grayscale_img2 = (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT / 2, MALLOC_CAP_SPIRAM);
  if (grayscale_img2 == NULL) {
     ESP_LOGE("epd_task", "Could not allocate framebuffer in PSRAM!");
  }

  // base image
  base_image = (uint8_t *)heap_caps_malloc(EPD_WIDTH / 2 * EPD_HEIGHT * 4, MALLOC_CAP_SPIRAM);
  if (base_image == NULL) {
     ESP_LOGE("epd_task", "Could not allocate framebuffer in PSRAM!");
  }
  memset(base_image, 255, EPD_WIDTH / 2 * EPD_HEIGHT);

  uint8_t grayscale_line[EPD_WIDTH / 2];
  uint8_t value = 0;
  for (uint32_t i = 0; i < EPD_WIDTH / 2; i++) {
    uint8_t segment = i / (EPD_WIDTH / 16 / 2);
    grayscale_line[i] = (segment << 4) | segment;
  }
  for (uint32_t y = 0; y < EPD_HEIGHT; y++) {
      memcpy(grayscale_img + EPD_WIDTH / 2 * y, grayscale_line, EPD_WIDTH / 2);
  }

  value = 0;
  for (uint32_t i = 0; i < EPD_WIDTH / 2; i++) {
    uint8_t segment = (EPD_WIDTH / 2 - i - 1) / (EPD_WIDTH / 16 / 2);
    grayscale_line[i] = (segment << 4) | segment;
  }
  for (uint32_t y = 0; y < EPD_HEIGHT; y++) {
      memcpy(grayscale_img2 + EPD_WIDTH / 2 * y, grayscale_line, EPD_WIDTH / 2);
  }

  xTaskCreate(&epd_task, "epd task", 10000, NULL, 2, NULL);
}
