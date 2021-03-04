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
#include "epd_highlevel.h"
#include "dragon.h"
//#include "eink_ED047TC2.h"
#include "epdiy_ED097TC2.h"
//#include "test.h"

#define WAVEFORM &ED097TC2

EpdiyHighlevelState hl;


void write_grayscale_pattern(bool direction, uint8_t* fb) {
  static uint8_t grayscale_line[EPD_WIDTH / 2];
  if (direction) {
      for (uint32_t i = 0; i < EPD_WIDTH / 2; i++) {
        uint8_t segment = i / (EPD_WIDTH / 16 / 2);
        grayscale_line[i] = (segment << 4) | segment;
      }
  } else {
      for (uint32_t i = 0; i < EPD_WIDTH / 2; i++) {
        uint8_t segment = (EPD_WIDTH / 2 - i - 1) / (EPD_WIDTH / 16 / 2);
        grayscale_line[i] = (segment << 4) | segment;
      }
  }
  for (uint32_t y = 0; y < EPD_HEIGHT; y++) {
      memcpy(fb + EPD_WIDTH / 2 * y, grayscale_line, EPD_WIDTH / 2);
  }
}

void loop() {

  uint8_t* fb = epd_hl_get_framebuffer(&hl);

  write_grayscale_pattern(false, fb);

  int temperature = 20; //epd_ambient_temperature();

  epd_poweron();
  epd_clear();
  enum EpdDrawError err = epd_hl_update_screen(&hl, MODE_GC16, temperature);
  epd_poweroff();

  vTaskDelay(5000);

  write_grayscale_pattern(true, fb);

  epd_poweron();
  err = epd_hl_update_screen(&hl, MODE_GC16, temperature);
  epd_poweroff();


  vTaskDelay(100000);
}

void IRAM_ATTR app_main() {
  epd_init(EPD_OPTIONS_DEFAULT);
  heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
  heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
  hl = epd_hl_init(WAVEFORM);
  while (1) {
    loop();
  };
}
