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

#include "EPD.h"
#include "epd_temperature.h"
#include "firasans.h"
#include "font.h"
#include "giraffe.h"
#include "image.h"

/* Display State Machine */
enum ScreenState {
  CLEAR_SCREEN = 0,
  DRAW_SCREEN = 1,
  CLEAR_PARTIAL = 2,
  DRAW_FONTS = 3,
};

uint8_t *img_buf;

uint8_t *framebuffer;
uint8_t *original_image_ram;

void delay(uint32_t millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }

uint32_t millis() { return esp_timer_get_time() / 1000; }

void loop() {
  // Variables to set one time.
  static enum ScreenState _state = CLEAR_SCREEN;

  printf("current temperature: %f\n", epd_ambient_temperature());
  delay(300);
  epd_poweron();

  epd_clear();

  epd_draw_hline(20, 20, 1160, 0x00, framebuffer);
  epd_draw_hline(20, 800, 1160, 0x00, framebuffer);
  epd_draw_vline(20, 20, 781, 0x00, framebuffer);
  epd_draw_vline(1180, 20, 781, 0x00, framebuffer);

  Rect_t area = {
      .x = 25,
      .y = 25,
      .width = giraffe_width,
      .height = giraffe_height,
  };
  epd_copy_to_framebuffer(area, (uint8_t *)giraffe_data, framebuffer);

  int cursor_x = 50 + giraffe_width + 20;
  int cursor_y = 100;
  writeln((GFXfont *)&FiraSans, (unsigned char *)"➸ 16 color grayscale",
          &cursor_x, &cursor_y, framebuffer);
  cursor_y += FiraSans.advance_y;
  cursor_x = 50 + giraffe_width + 20;
  writeln((GFXfont *)&FiraSans,
          (unsigned char *)"➸ ~650ms for full frame draw 🚀", &cursor_x,
          &cursor_y, framebuffer);
  cursor_y += FiraSans.advance_y;
  cursor_x = 50 + giraffe_width + 20;
  writeln((GFXfont *)&FiraSans, (unsigned char *)"➸ Use with 6\" or 9.7\" EPDs",
          &cursor_x, &cursor_y, framebuffer);
  cursor_y += FiraSans.advance_y;
  cursor_x = 50 + giraffe_width + 20;
  writeln((GFXfont *)&FiraSans,
          (unsigned char *)"➸ High-quality font rendering ✎🙋", &cursor_x,
          &cursor_y, framebuffer);

  epd_draw_grayscale_image(epd_full_screen(), framebuffer);

  delay(1000);
  cursor_x = 500;
  cursor_y = 600;
  unsigned char *string = (unsigned char *)"➠ With partial clear...";
  writeln((GFXfont *)&FiraSans, string, &cursor_x, &cursor_y, NULL);

  delay(1000);

  Rect_t to_clear = {
      .x = 50 + giraffe_width + 20,
      .y = 400,
      .width = 1200 - 70 - 25 - giraffe_width,
      .height = 400,
  };
  epd_clear_area(to_clear);

  cursor_x = 500;
  cursor_y = 600;
  string = (unsigned char *)"And partial update!";
  writeln((GFXfont *)&FiraSans, string, &cursor_x, &cursor_y, NULL);
  epd_poweroff();

  delay(2000);
}

void epd_task() {
  epd_init();
  epd_temperature_init();

  ESP_LOGW("main", "allocating...\n");

  original_image_ram =
      (uint8_t *)heap_caps_malloc(1200 * 825 / 2, MALLOC_CAP_SPIRAM);
  framebuffer = (uint8_t *)heap_caps_malloc(1200 * 825 / 2, MALLOC_CAP_SPIRAM);
  memset(framebuffer, 0xFF, 1200 * 825 / 2);

  volatile uint32_t t = millis();
  memcpy(original_image_ram, img_bytes, 1200 * 825 / 2);
  volatile uint32_t t2 = millis();
  printf("original copy to PSRAM took %dms.\n", t2 - t);

  // img_buf = (uint8_t *)heap_caps_malloc(1200 * 825 * 2, MALLOC_CAP_SPIRAM);

  // t = millis();
  // img_8bit_to_unary_image(img_buf, original_image_ram, 1200, 825);
  // t2 = millis();
  // printf("converting took %dms.\n", t2 - t);

  while (1) {
    loop();
  };
}

void app_main() {
  ESP_LOGW("main", "Hello World!\n");

  heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
  heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);

  xTaskCreatePinnedToCore(&epd_task, "epd task", 10000, NULL, 2, NULL, 1);
}
