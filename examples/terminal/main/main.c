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

typedef struct {
	char mask;    /* char data will be bitwise AND with this */
	char lead;    /* start bytes of current char in utf-8 encoded character */
	uint32_t beg; /* beginning of codepoint range */
	uint32_t end; /* end of codepoint range */
	int bits_stored; /* the number of bits from the codepoint that fits in char */
}utf_t;

static utf_t * utf[] = {
	/*             mask        lead        beg      end       bits */
	[0] = &(utf_t){0b00111111, 0b10000000, 0,       0,        6    },
	[1] = &(utf_t){0b01111111, 0b00000000, 0000,    0177,     7    },
	[2] = &(utf_t){0b00011111, 0b11000000, 0200,    03777,    5    },
	[3] = &(utf_t){0b00001111, 0b11100000, 04000,   0177777,  4    },
	[4] = &(utf_t){0b00000111, 0b11110000, 0200000, 04177777, 3    },
	      &(utf_t){0},
};

int codepoint_len(const uint32_t cp)
{
	int len = 0;
	for(utf_t **u = utf; *u; ++u) {
		if((cp >= (*u)->beg) && (cp <= (*u)->end)) {
			break;
		}
		++len;
	}
	if(len > 4) /* Out of bounds */
		exit(1);

	return len;
}

void to_utf8(char chr[5], const uint32_t cp)
{
	const int bytes = codepoint_len(cp);

	int shift = utf[0]->bits_stored * (bytes - 1);
	chr[0] = (cp >> shift & utf[bytes]->mask) | utf[bytes]->lead;
	shift -= utf[0]->bits_stored;
	for(int i = 1; i < bytes; ++i) {
		chr[i] = (cp >> shift & utf[0]->mask) | utf[0]->lead;
		shift -= utf[0]->bits_stored;
	}
	chr[bytes] = '\0';
}

static int utf8_len(uint8_t ch)
{
	int len = 0;
	for(utf_t **u = utf; *u; ++u) {
		if((ch & ~(*u)->mask) == (*u)->lead) {
			break;
		}
		++len;
	}
	if(len > 4) { /* Malformed leading byte */
		exit(1);
	}
	return len;
}

uint32_t to_cp(const char chr[4])
{
	int bytes = utf8_len(*chr);
	int shift = utf[0]->bits_stored * (bytes - 1);
	uint32_t codep = (*chr++ & utf[bytes]->mask) << shift;

	for(int i = 1; i < bytes; ++i, ++chr) {
		shift -= utf[0]->bits_stored;
		codep |= ((char)*chr & utf[0]->mask) << shift;
	}

	return codep;
}



// inspired by the st - the simple terminal, suckless.org

// A line is a sequence of code points.
typedef uint32_t* Line;

typedef struct {
    int x;
    int y;
} Cursor;

typedef struct {
    /// Number of rows.
    int row;
    /// Number of columns.
    int col;
    Line* line;
    Cursor cursor;
} Term;


static uint8_t uart_str_buffer[BUF_SIZE];
static uint8_t* uart_buffer_end = uart_str_buffer;
static uint8_t* uart_buffer_start = uart_str_buffer;
uint32_t read_char() {
    int remaining = uart_buffer_end - uart_buffer_start;
    if (uart_buffer_start >= uart_buffer_end
            || utf8_len(*uart_buffer_start) > remaining) {

        memmove(uart_str_buffer, uart_buffer_start, remaining);
        uart_buffer_start = uart_str_buffer;
        uart_buffer_end = uart_buffer_start + remaining;
        int unfilled = uart_str_buffer + BUF_SIZE - uart_buffer_end;
        int len = uart_read_bytes(UART_NUM_1, uart_buffer_end, unfilled, 20 / portTICK_RATE_MS);
        uart_buffer_end += len;
        if (len < 0) {
            ESP_LOGE("terminal", "uart read error");
            return 0;
        } else if (len == 0) {
            return 0;
        }
        remaining = uart_buffer_end - uart_buffer_start;
    }

    int bytes = utf8_len(*uart_buffer_start);
    if (remaining < bytes) {
      return 0;
    }
    int shift = utf[0]->bits_stored * (bytes - 1);
    uint32_t codep = (*uart_buffer_start++ & utf[bytes]->mask) << shift;

    for (int i = 1; i < bytes; ++i, ++uart_buffer_start) {
      shift -= utf[0]->bits_stored;
      codep |= ((uint8_t)*uart_buffer_start & utf[0]->mask) << shift;
    }
    return codep;
}

int calculate_horizontal_advance(GFXfont* font, Line line, int col) {
  int total = 0;
  for (int i = 0; i < col; i++) {
    int cp = line[i];
    GFXglyph* glyph;
    get_glyph(font, cp, &glyph);

    if (!glyph) {
      ESP_LOGW("terminal", "no glyph for %d", cp);
    }
    total += glyph->advance_x;
  }
  return total;
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


    uint8_t *framebuffer = (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT / 2, MALLOC_CAP_SPIRAM);
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

    delay(1000);

    uart_write_bytes(UART_NUM_1, "listening\n", 11);

    int line_start_x = 50;
    int cur_x = line_start_x;
    int cur_y = 100;

    uint32_t current_string[256] = {0};
    int current_str_index = 0;

    while (true) {

        uint32_t chr = read_char();
        if (chr > 0) {
          ESP_LOGI("terminal", "read char %d", chr);
        }


        char data[5];
        to_utf8(data, chr);

        // FIXME: handle control characters in mid-stream
        if (chr == '\b') {
          current_str_index--;
          char old_data[5];
          to_utf8(old_data, current_string[current_str_index]);
          current_string[current_str_index] = 0;

          int new_horizontal_advance = calculate_horizontal_advance((GFXfont *) &FiraSans, current_string, current_str_index);
          int new_cur_x = new_horizontal_advance + line_start_x;

          // we assume horizontal scripts
          cur_x = new_cur_x;

          epd_poweron();
          write_mode((GFXfont *) &FiraSans, (char *) old_data, &new_cur_x, &cur_y, NULL, WHITE_ON_WHITE);
          epd_poweroff();
        } else if (chr == '\r') {
          cur_x = 100;
        } else if (chr == '\n') {
          cur_y += ((GFXfont *)&FiraSans)->advance_y;
        } else if (chr > 0) {
            current_string[current_str_index] = chr;
            current_str_index++;

            uint32_t t1 = millis();
            int tmp_cur_x = cur_x;
            int tmp_cur_y = cur_y;
            writeln((GFXfont *) &FiraSans, (char *) data, &tmp_cur_x, &tmp_cur_y, framebuffer);

            epd_poweron();
            writeln((GFXfont *) &FiraSans, (char *) data, &cur_x, &cur_y, NULL);
            epd_poweroff();

            uint32_t t2 = millis();
            ESP_LOGI("main", "overall rendering took %dms.\n", t2 - t1);
            uart_write_bytes(UART_NUM_1, (const char *) data, 1);
        }
    }
    free(framebuffer);
}

void app_main() {
  xTaskCreatePinnedToCore(&epd_task, "epd task", 10000, NULL, 2, NULL, 1);
}
