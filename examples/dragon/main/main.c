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

#ifndef ARDUINO_ARCH_ESP32
void delay(uint32_t millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }
uint32_t millis() { return esp_timer_get_time() / 1000; }
#endif

void idf_loop() {
  epd_poweron();
  epd_clear();
  volatile uint32_t t1 = millis();
  epd_push_pixels(epd_full_screen(), 20, 0);
  epd_push_pixels(epd_full_screen(), 20, 0);
  epd_push_pixels(epd_full_screen(), 20, 0);
  epd_draw_image(epd_full_screen(), img_buf, WHITE_ON_BLACK);
  volatile uint32_t t2 = millis();
  printf("EPD draw took %dms.\n", t2 - t1);

  epd_poweroff();

  delay(5000);
}

void epd_task() {

  while (1) {
    idf_loop();
  };
}

void idf_setup() {

  // copy the image data to SRAM for faster display time.
  // This could also be rendered directly from flash.
  img_buf = (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT / 2, MALLOC_CAP_SPIRAM);
  if (img_buf == NULL) {
     ESP_LOGE("epd_task", "Could not allocate framebuffer in PSRAM!");
  }

  heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
  heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);


  memset(img_buf, 0, EPD_WIDTH * EPD_HEIGHT / 2);
  Rect_t image_size = {
      .width = dragon_width,
      .height = dragon_height,
      .x = 0,
      .y = 0,
  };
  epd_copy_to_framebuffer(image_size, dragon_data, img_buf);

  epd_init();
}



#ifndef ARDUINO_ARCH_ESP32
void app_main() {
  idf_setup();
  xTaskCreatePinnedToCore(&epd_task, "epd task", 10000, NULL, 2, NULL, 1);
}
#endif
