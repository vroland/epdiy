#pragma once

#include <stdint.h>

typedef struct {
    int width;
    int height;

    uint8_t bus_width;
} EpdDisplay_t;

const EpdDisplay_t ED060XC3 {
    .width = 1024,
    .height = 768,
    .bus_width = 8
};

const EpdDisplay_t ED097OC4 {
    .width = 1200,
    .height = 825,
    .bus_width = 8
};

const EpdDisplay_t ED097TC2 {
    .width = 1200,
    .height = 825,
    .bus_width = 8
};
