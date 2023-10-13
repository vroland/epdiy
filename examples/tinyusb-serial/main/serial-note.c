/* Simple firmware for a ESP32S3 USB data communication (v7 hardware branch: s3_lcb)
 * DOC: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/usb_device.html
 * IDF: examples/peripherals/usb/device/tusb_serial_device
**/

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdint.h>
#include "esp_log.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
//#include "sdkconfig.h"
#include "epd_highlevel.h"
#include "epdiy.h"
#include "firasans_20.h"
int temperature = 25;
EpdFontProperties font_props;
int cursor_x = 10;
int cursor_y = 30;

static const char *TAG = "USB serial device";
static uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1];

EpdiyHighlevelState hl;

void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    /* initialization */
    size_t rx_size = 0;

    /* read */
    esp_err_t ret = tinyusb_cdcacm_read(itf, buf, CONFIG_TINYUSB_CDC_RX_BUFSIZE, &rx_size);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Data from channel %d:", itf);
        uint8_t* fb = epd_hl_get_framebuffer(&hl);
        int temp_y = cursor_y;

        epd_poweron();
        temperature = epd_ambient_temperature();
        char strbuf[520];
        sprintf(strbuf, "%s", buf);
        epd_write_string(&FiraSans_20, strbuf, &cursor_x, &cursor_y, fb, &font_props);
        // 0D: Carriage return
        if (buf[0] == 0x0D) { 
            cursor_y += 16;
            cursor_x = 10;
        } else {
            cursor_y = temp_y;
        }
        epd_hl_update_screen(&hl, MODE_DU, temperature);
        epd_poweroff();

        ESP_LOG_BUFFER_HEXDUMP(TAG, buf, rx_size, ESP_LOG_INFO);
    } else {
        ESP_LOGE(TAG, "Read error");
    }

    /* write back */
    tinyusb_cdcacm_write_queue(itf, buf, rx_size);
    tinyusb_cdcacm_write_flush(itf, 0);
}

void tinyusb_cdc_line_state_changed_callback(int itf, cdcacm_event_t *event)
{
    int dtr = event->line_state_changed_data.dtr;
    int rts = event->line_state_changed_data.rts;
    ESP_LOGI(TAG, "Line state changed on channel %d: DTR:%d, RTS:%d", itf, dtr, rts);
}

void app_main(void)
{
    printf("Serial example\n");
    // EPDiy init
    epd_init(&epd_board_v7, &ED097TC2, EPD_LUT_64K);
    epd_set_vcom(1760);
    hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);

    epd_poweron();
    epd_fullclear(&hl, temperature);
    epd_poweroff();
    
    EpdFontProperties font_props = epd_font_properties_default();
    font_props.flags = EPD_DRAW_ALIGN_CENTER;

    ESP_LOGI(TAG, "USB initialization");
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = NULL,
        .external_phy = false,
        .configuration_descriptor = NULL,
    };
        ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = 64,
        .callback_rx = &tinyusb_cdc_rx_callback, // the first way to register a callback
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL
    };

    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));
    // the second way to register a callback
    ESP_ERROR_CHECK(tinyusb_cdcacm_register_callback(
                        TINYUSB_CDC_ACM_0,
                        CDC_EVENT_LINE_STATE_CHANGED,
                        &tinyusb_cdc_line_state_changed_callback));

#if (CONFIG_TINYUSB_CDC_COUNT > 1)
    acm_cfg.cdc_port = TINYUSB_CDC_ACM_1;
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));
    ESP_ERROR_CHECK(tinyusb_cdcacm_register_callback(
                        TINYUSB_CDC_ACM_1,
                        CDC_EVENT_LINE_STATE_CHANGED,
                        &tinyusb_cdc_line_state_changed_callback));
#endif

    ESP_LOGI(TAG, "USB initialization DONE");
}