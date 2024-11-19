#ifndef EPD_H
#define EPD_H

#include "epdiy.h"
#include <esp_log.h>

typedef struct {
    int width;
    int height;
    int temperature;
} EpdData;

EpdData n_epd_data();
void n_epd_setup();
void n_epd_clear();
void n_epd_draw(uint8_t* content, int x, int y, int width, int height);
#endif /* EPD_H */
