/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen */

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "epd_driver.h"
#include "epd_highlevel.h"
#include "dragon.h"

EpdiyHighlevelState hl;
EpdDisplay epd_display;

void idf_loop() {

  EpdRect dragon_area = {
      .x = 0,
      .y = 0,
      .width = dragon_width,
      .height = dragon_height
  };

  int temperature = 25;

  epd_poweron();
  epd_fullclear(&hl, temperature);

  epd_copy_to_framebuffer(dragon_area, dragon_data, epd_hl_get_framebuffer(&hl));

  enum EpdDrawError _err = epd_hl_update_screen(&hl, MODE_GC16, temperature);
  epd_poweroff();

  vTaskDelay(1000);
}

void idf_setup() {
  epd_display.waveform = epdiy_ED047TC1;
  epd_display.width = 960;
  epd_display.height = 540;

  epd_init(EPD_OPTIONS_DEFAULT, epd_display);
  hl = epd_hl_init(&epd_display.waveform);
}

#ifndef ARDUINO_ARCH_ESP32
void app_main() {
  idf_setup();

  //while (1) {
    idf_loop();
  //};
}
#endif
