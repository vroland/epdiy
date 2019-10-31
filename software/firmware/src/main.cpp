/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen.
 *
 * Write an image into a header file using a 3...2...1...0 format per pixel,
 * for 4 bits color (16 colors - well, greys.) MSB first.  At 80 MHz, screen
 * clears execute in 1.075 seconds and images are drawn in 1.531 seconds.
 */
#include "Arduino.h"
#include "image.hpp"
//#include "beat.hpp"
#include "ed097oc4.h"


/* Screen Constants */
#define EPD_WIDTH     1200
#define EPD_HEIGHT    825
#define CLEAR_BYTE    0B10101010
#define DARK_BYTE     0B01010101

/* Display State Machine */
enum ScreenState {
        CLEAR_SCREEN = 0,
        DRAW_SCREEN = 1,
};

/* Contrast cycles in order of contrast (Darkest first).  */
const uint32_t contrast_cycles[15] = {
    2, 2, 2,
    2, 3, 3, 3,
    4, 4, 5, 5,
    5, 10, 30, 50
};
hw_timer_t * timer = NULL;

/* Setup serial and allocate memory for the Paperback pointer */
void setup()
{
    Serial.begin(115200);
    Serial.println("Blank screen!");

    init_gpios();
    //timer = timerBegin(1, 1, true);
    //timerAttachInterrupt(timer, &on_output_timer_expire, true);
}

void draw_byte(uint8_t byte, short time) {

    start_frame();
    fill_byte(byte);
    for (int i = 0; i < EPD_HEIGHT; ++i) {
        output_row(time, NULL, timer);
    }
    end_frame(timer);
}

void clear_screen() {
    const short white_time = 50;
    const short dark_time = 40;

    for (int i=0; i<8; i++) {
        draw_byte(CLEAR_BYTE, white_time);
    }
    for (int i=0; i<6; i++) {
        draw_byte(DARK_BYTE, dark_time);
    }
    for (int i=0; i<8; i++) {
        draw_byte(CLEAR_BYTE, white_time);
    }
}

/* Setup serial and allocate memory for the Paperback pointer */
void loop()
{

        // Variables to set one time.
        static ScreenState _state = CLEAR_SCREEN;

        delay(300);
        poweron();
        uint32_t timestamp = 0;
        if (_state == CLEAR_SCREEN) {
                Serial.println("Clear cycle.");
                timestamp = millis();
                clear_screen();
                _state = DRAW_SCREEN;

        } else if (_state == DRAW_SCREEN) {
                Serial.println("Draw cycle.");
                timestamp = millis();
                for (uint8_t k = 15; k > 1; --k) {
                    start_frame();
                    const uint8_t *dp = img_bytes;

                    uint8_t row[EPD_WIDTH/4];
                    // Height of the display
                    for (int i = 0; i < EPD_HEIGHT; ++i) {

                        // Width of the display, 4 Pixels each.
                        for (int j = 0; j < (EPD_WIDTH/4); ++j) {
                            uint8_t pixel = 0B00000000;
                            uint8_t value = *(dp++);
                            //value = ((j / 19) << 4) | (j / 19);
                            pixel |= ((value >> 4) < k) << 6;
                            pixel |= ((value & 0B00001111) < k) << 4;
                            value = *(dp++);
                            //value = ((j / 19) << 4) | (j / 19);
                            pixel |= ((value >> 4) < k) << 2;
                            pixel |= ((value & 0B00001111) < k) << 0;
                            row[j] = pixel;
                        }
                        //
                        //uint32_t time = micros();
                        output_row(contrast_cycles[15 - k], (uint8_t*) &row, timer);
                        //Serial.println(micros() - time);
                        //delay(50);
                        //output_row(contrast_cycles[15 - k], (uint8_t*) &row, timer);
                    }
                    end_frame(timer);
                    //delay(1000);

                } // End loop of Refresh Cycles Size

                _state = CLEAR_SCREEN;
        }
        poweroff();
        // Print out the benchmark
        timestamp = millis() - timestamp;
        Serial.print("Took ");
        Serial.print(timestamp);
        Serial.println(" ms to redraw the screen.");

        // Wait 5 seconds then do it again
        delay(5000);
        Serial.println("Going active again.");
}
