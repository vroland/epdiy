/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen.
 *
 * Write an image into a header file using a 3...2...1...0 format per pixel,
 * for 4 bits color (16 colors - well, greys.) MSB first.  At 80 MHz, screen
 * clears execute in 1.075 seconds and images are drawn in 1.531 seconds.
 */
#include "Arduino.h"
#include "image.hpp"
//#include "beat.hpp"
#include "Paperback.hpp"


/* Screen Constants */
#define EPD_WIDTH     1200
#define EPD_HEIGHT    825
#define CLK_DELAY_US  1
#define VCLK_DELAY_US 0
#define OUTPUT_TIME   2
#define CLEAR_BYTE    0B10101010
#define DARK_BYTE     0B01010101

/* Display State Machine */
enum ScreenState {
        CLEAR_SCREEN = 0,
        DRAW_SCREEN = 1,
};

/* Pointer to the EPaper object */
Paperback *EPD;

/* Contrast cycles in order of contrast (Darkest first).  */
const uint8_t contrast_cycles[] = {2,2,1,1};
const uint8_t sz_contrast_cycles = sizeof(contrast_cycles)/sizeof(uint8_t);

/* Screen clearing state */
const uint8_t clear_cycles[] = {
  CLEAR_BYTE, CLEAR_BYTE, CLEAR_BYTE, CLEAR_BYTE, CLEAR_BYTE,
  CLEAR_BYTE, CLEAR_BYTE, CLEAR_BYTE,
  DARK_BYTE, DARK_BYTE, DARK_BYTE, DARK_BYTE, DARK_BYTE, DARK_BYTE,
  CLEAR_BYTE, CLEAR_BYTE, CLEAR_BYTE, CLEAR_BYTE, CLEAR_BYTE,
  CLEAR_BYTE, CLEAR_BYTE, CLEAR_BYTE,
  CLEAR_BYTE,
};
const uint8_t sz_clear_cycles = sizeof(clear_cycles)/sizeof(uint8_t);

/* Setup serial and allocate memory for the Paperback pointer */
void setup()
{
        Serial.begin(115200);
        Serial.println("Blank screen!");

        EPD = new Paperback(
                CLK_DELAY_US,
                VCLK_DELAY_US,
                OUTPUT_TIME
        );


        EPD->poweron();
}


/* Setup serial and allocate memory for the Paperback pointer */
void loop()
{

        // Variables to set one time.
        static ScreenState _state = CLEAR_SCREEN;

        delay(300);
        EPD->poweron();


        uint32_t timestamp = 0;
        if (_state == CLEAR_SCREEN) {
                Serial.println("Clear cycle.");
                timestamp = millis();
                data_output(CLEAR_BYTE);
                for (int k = 0; k < sz_clear_cycles; ++k) {
                        EPD->vscan_start();
                        data_output(clear_cycles[k]);
                        // Height of the display
                        for (int i = 0; i < EPD_HEIGHT; ++i) {
                                EPD->hscan_start();

                                // Width of the display, 4 Pixels each.
                                for (int j = 0; j < (EPD_WIDTH/4); ++j) {
                                        clock_pixel();
                                }

                                EPD->hscan_end();
                                EPD->output_row();
                                EPD->latch_row();
                        }

                        EPD->vscan_end();

                } // End loop of Refresh Cycles Size
                _state = DRAW_SCREEN;

        } else if (_state == DRAW_SCREEN) {
                Serial.println("Draw cycle.");
                timestamp = millis();
                for (int k = 0; k < sz_contrast_cycles; ++k) {
                        for (
                            int contrast_cnt = 0;
                            contrast_cnt < contrast_cycles[k];
                            ++contrast_cnt
                        ) {

                                EPD->vscan_start();
                                const uint8_t *dp = img_bytes;

                                // Height of the display
                                for (int i = 0; i < EPD_HEIGHT; ++i) {
                                        EPD->hscan_start();
                                        yield();

                                        // Width of the display, 4 Pixels each.
                                        for (
                                            int j = 0;
                                            j < (EPD_WIDTH/4);
                                            ++j
                                        ) {



                                                uint8_t pixel = 0B00000000;
                                                if (i < 600 && j < 200) {
                                                    uint8_t pix1 = \
                                                        (*(dp) >> 7 - k) & 1;
                                                    uint8_t pix2 = \
                                                        (*(dp++) >> 3 - k) & 1;
                                                    uint8_t pix3 = \
                                                        (*(dp) >> 7 - k) & 1;
                                                    uint8_t pix4 = \
                                                        (*(dp++) >> 3 - k) & 1;

                                                    pixel |= \
                                                        ( pixel_to_epd_cmd(
                                                            pix1
                                                        ) << 6
                                                    );
                                                    pixel |=
                                                        ( pixel_to_epd_cmd(
                                                            pix2
                                                        ) << 4
                                                    );
                                                    pixel |= \
                                                        ( pixel_to_epd_cmd(
                                                            pix3
                                                        ) << 2
                                                    );
                                                    pixel |= \
                                                        ( pixel_to_epd_cmd(
                                                            pix4
                                                        ) << 0
                                                    );
                                                } else {
                                                    pixel = DARK_BYTE;
                                                }

                                                data_output(pixel);
                                                clock_pixel();
                                        }

                                        EPD->hscan_end();
                                        EPD->output_row();
                                        EPD->latch_row();
                                }

                                EPD->vscan_end();
                        } // End contrast count

                } // End loop of Refresh Cycles Size

                _state = CLEAR_SCREEN;
        }

        // Print out the benchmark
        timestamp = millis() - timestamp;
        Serial.print("Took ");
        Serial.print(timestamp);
        Serial.println(" ms to redraw the screen.");
        EPD->poweroff();

        // Wait 5 seconds then do it again
        delay(5000);
        Serial.println("Going active again.");
}
