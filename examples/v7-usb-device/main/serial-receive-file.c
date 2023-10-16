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
uint8_t* fb;
static const char *TAG = "USB serial device";
static uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1];


EpdiyHighlevelState hl;

void epd_write_buf(char * buf) {
    epd_poweron();
    cursor_y += 16;
    epd_write_string(&FiraSans_20, buf, &cursor_x, &cursor_y, fb, &font_props);
    epd_hl_update_screen(&hl, MODE_DU, temperature);
    epd_poweroff();
}

uint16_t received_bytes = 0;

void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    /* initialization */
    size_t rx_size = 0;

    /* read */
    esp_err_t ret = tinyusb_cdcacm_read(itf, buf, CONFIG_TINYUSB_CDC_RX_BUFSIZE, &rx_size);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Data from channel %d:", itf);
        int temp_y = cursor_y;

        //temperature = epd_ambient_temperature();
        char strbuf[520];

        received_bytes += rx_size;
        sprintf(strbuf, "%d ", received_bytes);
        
        epd_write_string(&FiraSans_20, strbuf, &cursor_x, &cursor_y, fb, &font_props);

        epd_hl_update_screen(&hl, MODE_DU, temperature);
    
        if (cursor_x > epd_width() -20) { 
            cursor_y += 16;
            cursor_x = 10;
        } else {
            cursor_y = temp_y;
        }

        //ESP_LOG_BUFFER_HEXDUMP(TAG, buf, rx_size, ESP_LOG_INFO);
    } else {
        ESP_LOGE(TAG, "Read error");
    }

    /* write back. Do I need to return an echo? */
    tinyusb_cdcacm_write_queue(itf, buf, rx_size);
    tinyusb_cdcacm_write_flush(itf, 0);
}

void tinyusb_cdc_line_state_changed_callback(int itf, cdcacm_event_t *event)
{
    int dtr = event->line_state_changed_data.dtr;
    int rts = event->line_state_changed_data.rts;
    ESP_LOGI(TAG, "Line state changed on channel %d: DTR:%d, RTS:%d", itf, dtr, rts);
}

//------------- Array of String Descriptors -------------//
const char *descriptor_str[] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},                   // 0: is supported language is English (0x0409)
    "epdiy",                                // 1: Manufacturer
    "CDC USB file",                         // 2: Product
    CONFIG_TINYUSB_DESC_SERIAL_STRING,      // 3: Serials, should use chip ID

#if CONFIG_TINYUSB_CDC_ENABLED
    CONFIG_TINYUSB_DESC_CDC_STRING,          // 4: CDC Interface
#else
    "",
#endif

#if CONFIG_TINYUSB_MSC_ENABLED
    CONFIG_TINYUSB_DESC_MSC_STRING,          // 5: MSC Interface
#else
    "",
#endif
#if CONFIG_TINYUSB_NET_MODE_ECM_RNDIS || CONFIG_TINYUSB_NET_MODE_NCM
    "USB net",                               // 6. NET Interface
    "",                                      // 7. MAC
#endif
    NULL                                     // NULL: Must be last. Indicates end of array
};

void app_main(void)
{
    printf("Serial example\n");
    // EPDiy init
    epd_init(&epd_board_v7, &ED097TC2, EPD_LUT_64K);
    epd_set_vcom(1760);
    hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    fb = epd_hl_get_framebuffer(&hl);

    epd_poweron();
    epd_fullclear(&hl, temperature);
    
    EpdFontProperties font_props;
    font_props = epd_font_properties_default();
    font_props.flags = EPD_DRAW_ALIGN_CENTER;

    ESP_LOGI(TAG, "USB initialization");

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL, // Try also &usb_device
        .string_descriptor = &descriptor_str,
        .external_phy = false,
        .configuration_descriptor = NULL,
    };
        ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = 64,
        .callback_rx = &tinyusb_cdc_rx_callback, // Triggered on receive
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