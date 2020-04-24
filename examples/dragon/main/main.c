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

#include "epd_driver.h"
#include "dragon.h"

uint8_t *img_buf;

void delay(uint32_t millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }

uint32_t millis() { return esp_timer_get_time() / 1000; }

void loop() {
  epd_poweron();
  epd_clear();
  volatile uint32_t t1 = millis();
  epd_draw_grayscale_image(epd_full_screen(), img_buf);
  volatile uint32_t t2 = millis();
  printf("EPD draw took %dms.\n", t2 - t1);

  epd_poweroff();

  delay(5000);
}

void epd_task() {
  epd_init();

  while (1) {
    loop();
  };
}

void app_main() {
  // copy the image data to SRAM for faster display time
  img_buf = (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT / 2, MALLOC_CAP_SPIRAM);
  if (img_buf == NULL) {
     ESP_LOGE("epd_task", "Could not allocate framebuffer in PSRAM!");
  }

  memcpy(img_buf, dragon_data, EPD_WIDTH * EPD_HEIGHT / 2);
  heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
  heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);

  xTaskCreatePinnedToCore(&epd_task, "epd task", 10000, NULL, 2, NULL, 1);
}
