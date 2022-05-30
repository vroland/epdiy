#include "esp_log.h"
#include "epd_board.h"

void epd_temperature_init() {
  if (epd_board->temperature_init) {
    epd_board->temperature_init();
  }
}

float epd_ambient_temperature()
{
  if (!epd_board->ambient_temperature) {
    ESP_LOGW("epd_temperature", "No ambient temperature sensor - returning 21C");
    return 21.0;
  }
  return epd_board->ambient_temperature();
}
