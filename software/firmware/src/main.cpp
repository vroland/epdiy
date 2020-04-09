/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen.
 *
 * Write an image into a header file using a 3...2...1...0 format per pixel,
 * for 4 bits color (16 colors - well, greys.) MSB first.  At 80 MHz, screen
 * clears execute in 1.075 seconds and images are drawn in 1.531 seconds.
 */
#include "Arduino.h"

extern "C" {
#include "EPD.h"
#include "firasans.h"
#include "font.h"
#include "image.h"
}

/* Display State Machine */
enum ScreenState {
  CLEAR_SCREEN = 0,
  DRAW_SCREEN = 1,
  CLEAR_PARTIAL = 2,
  DRAW_SQUARES = 3,
};

uint8_t *img_buf;

void setup() {
  Serial.begin(115200);
  epd_init();

  img_buf = (uint8_t *)heap_caps_malloc(1200 * 825 * 2, MALLOC_CAP_SPIRAM);
  volatile uint32_t t = micros();
  img_8bit_to_unary_image(img_buf, (uint8_t*)img_bytes, 1200, 825);
  volatile uint32_t t2 = micros();
  printf("copy to PSRAM took %d us.\n", t2 - t);

  heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
  heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
}

void loop() {
  // Variables to set one time.
  static ScreenState _state = CLEAR_SCREEN;

  delay(300);
  epd_poweron();

  uint32_t timestamp = 0;
  if (_state == CLEAR_SCREEN) {
    Serial.println("Clear cycle.");
    Serial.flush();
    timestamp = millis();
    epd_clear();
    _state = DRAW_SCREEN;

  } else if (_state == DRAW_SCREEN) {
    Serial.println("Draw cycle.");
    timestamp = millis();
    draw_image_unary_coded(epd_full_screen(), img_buf);
    _state = CLEAR_PARTIAL;

  } else if (_state == CLEAR_PARTIAL) {
    Serial.println("Partial clear cycle.");
    timestamp = millis();
    Rect_t area = {
        .x = 100,
        .y = 100,
        .width = 1200 - 200,
        .height = 825 - 200,
    };
    epd_clear_area(area);
    _state = DRAW_SQUARES;
  } else if (_state == DRAW_SQUARES) {
    Serial.println("Squares cycle.");
    timestamp = millis();
    int cursor_x = 100;
    int cursor_y = 200;
    unsigned char *string = (unsigned char *)"Hello World! *g*";
    writeln((GFXfont *)&FiraSans, string, &cursor_x, &cursor_y);
    cursor_y += FiraSans.advance_y;
    string = (unsigned char *)"äöüßabcd/#{🚀";
    writeln((GFXfont *)&FiraSans, string, &cursor_x, &cursor_y);
    _state = CLEAR_SCREEN;
  }
  epd_poweroff();
  // Print out the benchmark
  timestamp = millis() - timestamp;
  Serial.print("Took ");
  Serial.print(timestamp);
  Serial.println(" ms to redraw the screen.");

  // Wait 4 seconds then do it again
  delay(2000);
  Serial.println("Going active again.");
}
