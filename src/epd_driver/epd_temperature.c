#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "display_ops.h"

#ifdef CONFIG_EPD_BOARD_REVISION_LILYGO_T5_47
void epd_temperature_init() {}
float epd_ambient_temperature()
{
  ESP_LOGW("epd_temperature", "No ambient temperature sensor - returning 21C");
  return 21.0;
}
#else
#ifndef CONFIG_EPD_BOARD_REVISION_V6
/// Use GPIO 35 for boards v4 - v5
static const adc1_channel_t channel = ADC1_CHANNEL_7;
static esp_adc_cal_characteristics_t adc_chars;

#define NUMBER_OF_SAMPLES 100

void epd_temperature_init()
{
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize(
      ADC_UNIT_1, ADC_ATTEN_DB_6, ADC_WIDTH_BIT_12, 1100, &adc_chars);
  if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP)
  {
    ESP_LOGI("epd_temperature", "Characterized using Two Point Value\n");
  }
  else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF)
  {
    ESP_LOGI("esp_temperature", "Characterized using eFuse Vref\n");
  }
  else
  {
    ESP_LOGI("esp_temperature", "Characterized using Default Vref\n");
  }
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(channel, ADC_ATTEN_DB_6);
}

float epd_ambient_temperature()
{
  uint32_t value = 0;
  for (int i = 0; i < NUMBER_OF_SAMPLES; i++)
  {
    value += adc1_get_raw(channel);
  }
  value /= NUMBER_OF_SAMPLES;
  // voltage in mV
  float voltage = esp_adc_cal_raw_to_voltage(value, &adc_chars);
  return (voltage - 500.0) / 10.0;
}
#elif defined(CONFIG_EPD_BOARD_REVISION_V6)

#include "tps65185.h"

void epd_temperature_init() {}

float epd_ambient_temperature()
{
  return tps_read_thermistor(EPDIY_I2C_PORT);
}

#endif
#endif
