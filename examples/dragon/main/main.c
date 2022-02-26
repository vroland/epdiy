/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen */

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "epd_driver.h"
#include "epd_highlevel.h"
// Display default configurations
#include "epd_displays.h"
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
  
  // A - Withouth using epd_displays.h and just setting display manually
  /* 
  epd_display.name = "Lilygo EPD47"; // optional
  epd_display.waveform = epdiy_ED047TC1;
  epd_display.width = 960;
  epd_display.height = 540; */

  // B - Alternatively use predefined default configuration (#include "epd_displays.h")
  // This will use a bit of extra instruction RAM since all struct are defined
  // We could do this in epd_init but then we cannot pass display settings there
  epdiy_display_default_configs();
  // Check definitions in epd_driver.c
  epd_display = ED047TC1;
  printf("Using display %s w:%d h:%d\n\n", epd_display.name, epd_display.width, epd_display.height);

  epd_init(EPD_OPTIONS_DEFAULT, epd_display);
  hl = epd_hl_init(&epd_display.waveform);
  epd_fullclear(&hl, 25);
}

#ifndef ARDUINO_ARCH_ESP32
void app_main() {
  idf_setup();

  //while (1) {
    idf_loop();
  //};
}
#endif
