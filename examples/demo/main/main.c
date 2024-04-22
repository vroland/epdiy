/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen.
 *
 * Write an image into a header file using a 3...2...1...0 format per pixel,
 * for 4 bits color (16 colors - well, greys.) MSB first.  At 80 MHz, screen
 * clears execute in 1.075 seconds and images are drawn in 1.531 seconds.
 */

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <epdiy.h>

#include "sdkconfig.h"

#include "firasans_12.h"
#include "firasans_20.h"
#include "img_beach.h"
#include "img_board.h"
#include "img_zebra.h"

#define WAVEFORM EPD_BUILTIN_WAVEFORM

// choose the default demo board depending on the architecture
#ifdef CONFIG_IDF_TARGET_ESP32
#define DEMO_BOARD epd_board_v6
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define DEMO_BOARD epd_board_v7
#endif

EpdiyHighlevelState hl;

void idf_setup() {
    epd_init(&DEMO_BOARD, &ED097TC2, EPD_LUT_64K);
    // Set VCOM for boards that allow to set this in software (in mV).
    // This will print an error if unsupported. In this case,
    // set VCOM using the hardware potentiometer and delete this line.
    epd_set_vcom(1560);

    hl = epd_hl_init(WAVEFORM);

    // Default orientation is EPD_ROT_LANDSCAPE
    epd_set_rotation(EPD_ROT_LANDSCAPE);

    printf(
        "Dimensions after rotation, width: %d height: %d\n\n", epd_rotated_display_width(),
        epd_rotated_display_height()
    );

    // The display bus settings for V7 may be conservative, you can manually
    // override the bus speed to tune for speed, i.e., if you set the PSRAM speed
    // to 120MHz.
    // epd_set_lcd_pixel_clock_MHz(17);

    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
    heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
}

#ifndef ARDUINO_ARCH_ESP32
void delay(uint32_t millis) {
    vTaskDelay(millis / portTICK_PERIOD_MS);
}
#endif

static inline void checkError(enum EpdDrawError err) {
    if (err != EPD_DRAW_SUCCESS) {
        ESP_LOGE("demo", "draw error: %X", err);
    }
}

void draw_progress_bar(int x, int y, int width, int percent, uint8_t* fb) {
    const uint8_t white = 0xFF;
    const uint8_t black = 0x0;

    EpdRect border = {
        .x = x,
        .y = y,
        .width = width,
        .height = 20,
    };
    epd_fill_rect(border, white, fb);
    epd_draw_rect(border, black, fb);

    EpdRect bar = {
        .x = x + 5,
        .y = y + 5,
        .width = (width - 10) * percent / 100,
        .height = 10,
    };

    epd_fill_rect(bar, black, fb);

    checkError(epd_hl_update_area(&hl, MODE_DU, epd_ambient_temperature(), border));
}

void idf_loop() {
    // select the font based on display width
    const EpdFont* font;
    if (epd_width() < 1000) {
        font = &FiraSans_12;
    } else {
        font = &FiraSans_20;
    }

    uint8_t* fb = epd_hl_get_framebuffer(&hl);

    epd_poweron();
    epd_clear();
    int temperature = epd_ambient_temperature();
    epd_poweroff();

    printf("current temperature: %d\n", temperature);

    epd_fill_circle(30, 30, 15, 0, fb);
    int cursor_x = epd_rotated_display_width() / 2;
    int cursor_y = epd_rotated_display_height() / 2 - 100;

    EpdFontProperties font_props = epd_font_properties_default();
    font_props.flags = EPD_DRAW_ALIGN_CENTER;

    char srotation[32];
    sprintf(srotation, "Loading demo...\nRotation: %d", epd_get_rotation());

    epd_write_string(font, srotation, &cursor_x, &cursor_y, fb, &font_props);

    int bar_x = epd_rotated_display_width() / 2 - 200;
    int bar_y = epd_rotated_display_height() / 2;

    epd_poweron();
    checkError(epd_hl_update_screen(&hl, MODE_GL16, temperature));
    epd_poweroff();

    for (int i = 0; i < 6; i++) {
        epd_poweron();
        draw_progress_bar(bar_x, bar_y, 400, i * 10, fb);
        epd_poweroff();
    }

    cursor_x = epd_rotated_display_width() / 2;
    cursor_y = epd_rotated_display_height() / 2 + 100;

    epd_write_string(
        font, "Just kidding,\n this is a demo animation 😉", &cursor_x, &cursor_y, fb, &font_props
    );
    epd_poweron();
    checkError(epd_hl_update_screen(&hl, MODE_GL16, temperature));
    epd_poweroff();

    for (int i = 0; i < 6; i++) {
        epd_poweron();
        draw_progress_bar(bar_x, bar_y, 400, 50 - i * 10, fb);
        epd_poweroff();
        vTaskDelay(1);
    }

    cursor_y = epd_rotated_display_height() / 2 + 200;
    cursor_x = epd_rotated_display_width() / 2;

    EpdRect clear_area = {
        .x = 0,
        .y = epd_rotated_display_height() / 2 + 100,
        .width = epd_rotated_display_width(),
        .height = 300,
    };

    epd_fill_rect(clear_area, 0xFF, fb);

    epd_write_string(
        font, "Now let's look at some pictures.", &cursor_x, &cursor_y, fb, &font_props
    );
    epd_poweron();
    checkError(epd_hl_update_screen(&hl, MODE_GL16, temperature));
    epd_poweroff();

    delay(1000);

    epd_hl_set_all_white(&hl);

    EpdRect zebra_area = {
        .x = epd_rotated_display_width() / 2 - img_zebra_width / 2,
        .y = epd_rotated_display_height() / 2 - img_zebra_height / 2,
        .width = img_zebra_width,
        .height = img_zebra_height,
    };

    epd_draw_rotated_image(zebra_area, img_zebra_data, fb);
    epd_poweron();
    checkError(epd_hl_update_screen(&hl, MODE_GC16, temperature));
    epd_poweroff();

    delay(5000);

    EpdRect board_area = {
        .x = epd_rotated_display_width() / 2 - img_board_width / 2,
        .y = epd_rotated_display_height() / 2 - img_board_height / 2,
        .width = img_board_width,
        .height = img_board_height,
    };

    epd_draw_rotated_image(board_area, img_board_data, fb);
    cursor_x = epd_rotated_display_width() / 2;
    cursor_y = board_area.y;
    font_props.flags |= EPD_DRAW_BACKGROUND;
    epd_write_string(font, "↓ Thats the V2 board. ↓", &cursor_x, &cursor_y, fb, &font_props);

    epd_poweron();
    checkError(epd_hl_update_screen(&hl, MODE_GC16, temperature));
    epd_poweroff();

    delay(5000);
    epd_hl_set_all_white(&hl);

    EpdRect border_rect = {
        .x = 20,
        .y = 20,
        .width = epd_rotated_display_width() - 40,
        .height = epd_rotated_display_height() - 40};
    epd_draw_rect(border_rect, 0, fb);

    cursor_x = 50;
    cursor_y = 100;

    epd_write_default(
        font,
        "➸ 16 color grayscale\n"
        "➸ ~250ms - 1700ms for full frame draw 🚀\n"
        "➸ Use with 6\" or 9.7\" EPDs\n"
        "➸ High-quality font rendering ✎🙋\n"
        "➸ Partial update\n"
        "➸ Arbitrary transitions with vendor waveforms",
        &cursor_x, &cursor_y, fb
    );

    EpdRect img_beach_area = {
        .x = 0,
        .y = epd_rotated_display_height() - img_beach_height,
        .width = img_beach_width,
        .height = img_beach_height,
    };

    epd_draw_rotated_image(img_beach_area, img_beach_data, fb);

    epd_poweron();
    checkError(epd_hl_update_screen(&hl, MODE_GC16, temperature));
    epd_poweroff();

    printf("going to sleep...\n");
    epd_deinit();
    esp_deep_sleep_start();
}

#ifndef ARDUINO_ARCH_ESP32
void app_main() {
    idf_setup();

    while (1) {
        idf_loop();
    };
}
#endif
