/* Simple firmware for a ESP32 displaying a Color square in a DES epaper screen with CFA on top */
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#include "esp_sleep.h"
#include <epdiy.h>
#include "epd_highlevel.h"

EpdiyHighlevelState hl;
int temperature = 25;
// Buffers
uint8_t *fb;            // EPD 2bpp buffer

int color_test() {
  EpdRect epd_area = {
      .x = 0,
      .y = 0,
      .width = epd_width(),
      .height = epd_height()
  };

  uint8_t color_of = 255;
  /**
   * @brief PATTERN
   * pix1     2    3  X
   *    B     G    R  row:1
   *    G     R    B  row:2
   *    R     B    G  row:3
   */
  for (uint16_t y=1; y<601; y++) {
    for (uint16_t x=1; x<epd_area.width; x++) {
      // x, y, r, g, b
      if (y < 200) {
        epd_draw_cpixel(x, y, x/6, color_of, color_of, hl.front_fb);
      } else if (y >= 200 && y < 400) {
        epd_draw_cpixel(x, y, color_of, x/6, color_of, hl.front_fb);
      } else {
        epd_draw_cpixel(x, y, color_of, color_of, x/6, hl.front_fb);
      }
    }
    //vTaskDelay(1);
  }
  

  enum EpdDrawError _err = epd_hl_update_screen(&hl, MODE_GC16, temperature);
  epd_poweroff();
  return _err;
}

// Deepsleep
#define DEEP_SLEEP_SECONDS 300
uint64_t USEC = 1000000;
int getFreePsram(){
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
    return info.total_free_bytes;
}

#ifndef ARDUINO_ARCH_ESP32
void app_main() {
  printf("before epd_init() Free PSRAM: %d\n", getFreePsram());

  epd_init(&epd_board_v7, &EC060KH3, EPD_LUT_64K);
  //epd_set_rotation(EPD_ROT_PORTRAIT);
  // Set VCOM for boards that allow to set this in software (in mV).
  epd_set_vcom(1560);
  hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
  fb = epd_hl_get_framebuffer(&hl);
  printf("after epd_hl_init() Free PSRAM: %d\n", getFreePsram());


  epd_poweron();
  epd_set_gamma_curve(0.4);
  epd_fullclear(&hl, 25);


  color_test();
  printf("color example\n");

  vTaskDelay(pdMS_TO_TICKS(5000));
  esp_sleep_enable_timer_wakeup(DEEP_SLEEP_SECONDS * USEC);
  esp_deep_sleep_start();
}

#endif
