/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen.
 *
 * Write an image into a header file using a 3...2...1...0 format per pixel,
 * for 4 bits color (16 colors - well, greys.) MSB first.  At 80 MHz, screen
 * clears execute in 1.075 seconds and images are drawn in 1.531 seconds.
 */
#include "Arduino.h"
#include "image.hpp"
#include "EPD.hpp"


/* Display State Machine */
enum ScreenState {
        CLEAR_SCREEN = 0,
        DRAW_SCREEN = 1,
        CLEAR_PARTIAL = 2,
};

EPD* epd;


void setup() {
    Serial.begin(115200);
    Serial.println("Blank screen!");

    epd = new EPD(1200, 825);
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
                for (int i=0; i < 10; i++) {
                    Rect_t area = {
                        .x = 100 + i,
                        .y = 100 + 30* i,
                        .width = epd->width - 200,
                        .height = 30,
                    };
                    epd->clear_area(area);
                }
                _state = CLEAR_SCREEN;
        }
        epd->poweroff();
        // Print out the benchmark
        timestamp = millis() - timestamp;
        Serial.print("Took ");
        Serial.print(timestamp);
        Serial.println(" ms to redraw the screen.");

        // Wait 5 seconds then do it again
        delay(3000);
        Serial.println("Going active again.");
}
