#include "epd_board.h"
#include "esp_log.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static const adc_channel_t channel = ADC_CHANNEL_7;

#define NUMBER_OF_SAMPLES 100

void epd_board_temperature_init_v2() {
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_6,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, channel, &chan_cfg));

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
    int successful_samples = 0;
    for (int i = 0; i < NUMBER_OF_SAMPLES; i++) {
        if (adc_oneshot_read(adc_handle, channel, &raw) == ESP_OK) {
            value += raw;
            successful_samples++;
        }
    }
    if (successful_samples == 0) {
        return 20.0f;
    }
    value /= successful_samples;
    int voltage_mv = 0;
    if (adc_cali_handle != NULL) {
        if (adc_cali_raw_to_voltage(adc_cali_handle, (int)value, &voltage_mv) != ESP_OK) {
            return 20.0f;
        }
    } else {
        voltage_mv = (int)(value * 2200 / 4095);
    }
    return ((float)voltage_mv - 500.0f) / 10.0f;
}

void epd_board_temperature_deinit_v2() {
    if (adc_handle != NULL) {
        ESP_ERROR_CHECK(adc_oneshot_del_unit(adc_handle));
        adc_handle = NULL;
    }
    if (adc_cali_handle != NULL) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(adc_cali_handle));
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(adc_cali_handle));
#endif
        adc_cali_handle = NULL;
    }
}
