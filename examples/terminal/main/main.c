/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen.
 *
 * Write an image into a header file using a 3...2...1...0 format per pixel,
 * for 4 bits color (16 colors - well, greys.) MSB first.  At 80 MHz, screen
 * clears execute in 1.075 seconds and images are drawn in 1.531 seconds.
 */

#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#include "epd_driver.h"
#ifdef CONFIG_EPD_DISPLAY_TYPE_ED060SC4
#include "firasans_12pt.h"
#else
#include "firasans.h"

#endif

#define ECHO_TEST_TXD  (GPIO_NUM_1)
#define ECHO_TEST_RXD  (GPIO_NUM_3)
#define ECHO_TEST_RTS  (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS  (UART_PIN_NO_CHANGE)

#define BUF_SIZE (1024)


void delay(uint32_t millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }

uint32_t millis() { return esp_timer_get_time() / 1000; }

int log_to_uart(const char* fmt, va_list args) {
    char buffer[256];
    int result = vsprintf(buffer, fmt, args);
    uart_write_bytes(UART_NUM_1, buffer, strnlen(buffer, 256));
    return result;
}

void epd_task() {
    epd_init();
    delay(300);
    epd_poweron();
    epd_clear();
    epd_poweroff();

    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
            .baud_rate = 115200,
            .data_bits = UART_DATA_8_BITS,
            .parity    = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS);
    uart_driver_install(UART_NUM_1, BUF_SIZE * 2, 0, 0, NULL, 0);

    // Still log to the serial output
    esp_log_set_vprintf(log_to_uart);

    // Configure a temporary buffer for the incoming data
    uint8_t *data = (uint8_t *) malloc(BUF_SIZE);
    uint8_t *framebuffer = (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT / 2, MALLOC_CAP_SPIRAM);
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

    delay(1000);

    uart_write_bytes(UART_NUM_1, "listening\n", 11);

    int cur_x = 100;
    int cur_y = 100;

    uint8_t current_string[256] = {0};
    uint8_t* current_string_ptr = current_string;

    while (true) {
        // Read data from the UART
        int len = uart_read_bytes(UART_NUM_1, data, BUF_SIZE - 1, 20 / portTICK_RATE_MS);
        data[len] = 0;


        // FIXME: handle control characters in mid-stream
        if (data[0] == '\b') {
            uint8_t skipped = 0;
            // skip a utf8 code point backwards
            do {
                current_string_ptr--;
                skipped++;
            } while ((*current_string_ptr & 0xC0) == 0x80);

            int tmp_cur_x = 0;
            int tmp_cur_y = 0;
            int x, y, w, h;
            get_text_bounds((GFXfont *) &FiraSans, (char *) current_string_ptr, &tmp_cur_x, &tmp_cur_y, &x, &y, &w, &h);

            int new_cur_x = cur_x - tmp_cur_x;
            int new_cur_y = cur_y - tmp_cur_y;
            cur_x = new_cur_x;
            cur_y = new_cur_y;

            epd_poweron();
            write_mode((GFXfont *) &FiraSans, (char *) current_string_ptr, &new_cur_x, &new_cur_y, NULL, WHITE_ON_WHITE);
            epd_poweroff();

            memset(current_string_ptr, 0, skipped);

        } else if (len > 0) {
            strncpy((char*)current_string_ptr, (char*)data, len);
            current_string_ptr += len;

            uint32_t t1 = millis();
            int tmp_cur_x = cur_x;
            int tmp_cur_y = cur_y;
            writeln((GFXfont *) &FiraSans, (char *) data, &tmp_cur_x, &tmp_cur_y, framebuffer);

            epd_poweron();
            writeln((GFXfont *) &FiraSans, (char *) data, &cur_x, &cur_y, NULL);
            epd_poweroff();

            uint32_t t2 = millis();
            ESP_LOGI("main", "overall rendering took %dms.\n", t2 - t1);
            uart_write_bytes(UART_NUM_1, (const char *) data, len);
        }
    }
    free(framebuffer);
}

void app_main() {
  xTaskCreatePinnedToCore(&epd_task, "epd task", 10000, NULL, 2, NULL, 1);
}
