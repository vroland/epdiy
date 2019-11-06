/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen.
 *
 * Write an image into a header file using a 3...2...1...0 format per pixel,
 * for 4 bits color (16 colors - well, greys.) MSB first.  At 80 MHz, screen
 * clears execute in 1.075 seconds and images are drawn in 1.531 seconds.
 */
#include "Arduino.h"
#include "image.hpp"
#include "EPD.hpp"
#include "zlib.h"
#include "gfxfont.h"
#include "firasans.h"

/* Display State Machine */
enum ScreenState {
        CLEAR_SCREEN = 0,
        DRAW_SCREEN = 1,
        CLEAR_PARTIAL = 2,
        DRAW_SQUARES = 3,
};

EPD* epd;

const uint8_t square[15] = {
    0B00000000, 0B00000000, 0B00000000,
    0B00001111, 0B11111111, 0B00000000,
    0B00001111, 0B11111111, 0B00000000,
    0B00001111, 0B11111111, 0B00000000,
    0B00000000, 0B00000000, 0B00000000,
};

uint8_t a_buf[1081];

void setup() {
    Serial.begin(115200);
    Serial.println("Blank screen!");

    epd = new EPD(1200, 825);
    unsigned long dsize = 1081;
    Serial.println(uncompress(a_buf, &dsize, glyph_A, sizeof(glyph_A)));
}

void loop() {
        // Variables to set one time.
        static ScreenState _state = CLEAR_SCREEN;

        delay(300);
        epd->poweron();
        uint32_t timestamp = 0;
        if (_state == CLEAR_SCREEN) {
                Serial.println("Clear cycle.");
                Serial.flush();
                timestamp = millis();
                epd->clear_screen();
                _state = DRAW_SCREEN;

        } else if (_state == DRAW_SCREEN) {
                Serial.println("Draw cycle.");
                timestamp = millis();
                epd->draw_picture(epd->full_screen(), (uint8_t*)&img_bytes);
                _state = CLEAR_PARTIAL;

        } else if (_state == CLEAR_PARTIAL) {
                Serial.println("Partial clear cycle.");
                timestamp = millis();
                Rect_t area = {
                    .x = 100,
                    .y = 100,
                    .width = epd->width - 200,
                    .height = epd->height - 200,
                };
                epd->clear_area(area);
                _state = DRAW_SQUARES;
        } else if (_state == DRAW_SQUARES) {
                Serial.println("Squares cycle.");
                timestamp = millis();
                for (int i=0; i<20; i++) {
                    Rect_t area = {
                        .x = 150 + 51 * i,
                        .y = 150 + 51 * i,
                        .width = 45,
                        .height = 47,
                    };
                    epd->draw_picture(area, (uint8_t*)a_buf);
                }
                _state = CLEAR_SCREEN;
        }
        epd->poweroff();
        // Print out the benchmark
        timestamp = millis() - timestamp;
        Serial.print("Took ");
        Serial.print(timestamp);
        Serial.println(" ms to redraw the screen.");

        // Wait 1 seconds then do it again
        delay(1000);
        Serial.println("Going active again.");
}
