/* 
 * A more high-level library for drawing to an EPD.
 */
#pragma once
#include "Arduino.h"

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} Rect_t;

class EPD {
    public:
        uint16_t width;
        uint16_t height;
        
        EPD(uint16_t width, uint16_t height);

        /* enable display power supply. */
        void poweron();
        /* disable display power supply. */
        void poweroff();

        /* clear the whole screen. */
        void clear_screen();

        /* Clear an area by flashing it white -> black -> white */
        void clear_area(Rect_t area); 

        /* 
         * Draw a picture to a given area. The picture must be given as
         * sequence of 4-bit brightness values, packed as two pixels per byte.
         * A byte cannot wrap over multiple rows, but if the image size is given as 
         * uneven, the last half byte is ignored.
         * The given area must be white before drawing.
         */
        void draw_picture(Rect_t area, uint8_t* data);

        /*
         * Returns a rectancle representing the whole screen area.
         */
        Rect_t full_screen();

        /* draw a frame with all pixels being set to `byte`,
         * where byte is an EPD-specific encoding of black and white. */
        void draw_byte(Rect_t* area, short time, uint8_t byte);

        ~EPD();
    protected:
        // A row with only null bytes, to be loaded when skipping lines
        // to avoid slight darkening / lightening.
        uint8_t* null_row;

        // status tracker for row skipping
        uint32_t skipping;

        // skip a display row
        void skip_row();

        // output a row to the display.
        void write_row(uint32_t output_time_us, uint8_t* data);
};
