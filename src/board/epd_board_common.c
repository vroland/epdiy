#include "epd_board.h"
#include "esp_log.h"

#include <esp_idf_version.h>

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
// IDF 6.x: legacy ADC driver removed, use oneshot driver + calibration
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static const adc_channel_t channel = ADC_CHANNEL_7;

#define NUMBER_OF_SAMPLES 100

void epd_board_temperature_init_v2() {
    // Initialize ADC oneshot unit
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    // Configure channel
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_6,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, channel, &chan_cfg));

    // Initialize calibration
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_6,
        .bitwidth = ADC_BITWIDTH_12,
    };
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &adc_cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_6,
        .bitwidth = ADC_BITWIDTH_12,
    };
    esp_err_t ret = adc_cali_create_scheme_line_fitting(&cali_cfg, &adc_cali_handle);
#else
    esp_err_t ret = ESP_ERR_NOT_SUPPORTED;
#endif
    if (ret == ESP_OK) {
        ESP_LOGI("epd_temperature", "ADC calibration scheme initialized");
    } else {
        ESP_LOGW("epd_temperature", "ADC calibration not available (err=%d), raw values used", ret);
    }
}

float epd_board_ambient_temperature_v2() {
    int raw = 0;
    uint32_t value = 0;
    for (int i = 0; i < NUMBER_OF_SAMPLES; i++) {
        if (adc_oneshot_read(adc_handle, channel, &raw) == ESP_OK) {
            value += raw;
        }
    }
    value /= NUMBER_OF_SAMPLES;
    // voltage in mV
    int voltage_mv = 0;
    if (adc_cali_handle != NULL) {
        adc_cali_raw_to_voltage(adc_cali_handle, (int)value, &voltage_mv);
    } else {
        // Fallback: rough estimate without calibration
        // 12-bit ADC with ~6dB attenuation gives roughly 0-2200mV range
        voltage_mv = (int)(value * 2200 / 4095);
    }
    return ((float)voltage_mv - 500.0f) / 10.0f;
}

#else
// IDF 4.x / 5.x: use legacy ADC driver
#include "driver/adc.h"
#include "esp_adc_cal.h"

static const adc1_channel_t channel = ADC1_CHANNEL_7;
static esp_adc_cal_characteristics_t adc_chars;

#define NUMBER_OF_SAMPLES 100

void epd_board_temperature_init_v2() {
    esp_adc_cal_value_t val_type
        = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_6, ADC_WIDTH_BIT_12, 1100, &adc_chars);
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        ESP_LOGI("epd_temperature", "Characterized using Two Point Value\n");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        ESP_LOGI("esp_temperature", "Characterized using eFuse Vref\n");
    } else {
        ESP_LOGI("esp_temperature", "Characterized using Default Vref\n");
    }
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(channel, ADC_ATTEN_DB_6);
}

float epd_board_ambient_temperature_v2() {
    uint32_t value = 0;
    for (int i = 0; i < NUMBER_OF_SAMPLES; i++) {
        value += adc1_get_raw(channel);
    }
    value /= NUMBER_OF_SAMPLES;
    // voltage in mV
    float voltage = esp_adc_cal_raw_to_voltage(value, &adc_chars);
    return (voltage - 500.0) / 10.0;
}
#endif
