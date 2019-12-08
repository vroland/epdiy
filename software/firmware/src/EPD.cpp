#include "EPD.hpp"
extern "C" {
    #include "ed097oc4.h"
}

#define CLEAR_BYTE    0B10101010
#define DARK_BYTE     0B01010101

/* Contrast cycles in order of contrast (Darkest first).  */
const uint32_t contrast_cycles[15] = {
    2, 2, 2,
    2, 3, 3, 3,
    4, 4, 5, 5,
    5, 10, 30, 50
};

EPD::EPD(uint16_t width, uint16_t height) {
    this->width = width;
    this->height = height;
    this->skipping = 0;
    this->null_row = (uint8_t*)malloc(this->width/4);
    for (int i = 0; i < this->width/4; i++) {
        this->null_row[i] = 255;
    }
    //memset(this->null_row, 0, this->width/4);
    init_gpios();
}

EPD::~EPD() {
    free(this->null_row);
}

void EPD::poweron() {
    epd_poweron();
}

void EPD::poweroff() {
    epd_poweroff();
}

void EPD::skip_row() {
    if (this->skipping < 1) {
        output_row(10, this->null_row, this->width);
    } else {
        output_row(10, this->null_row, this->width);
        //skip(this->width);
    }
    this->skipping++;
}

void EPD::write_row(uint32_t output_time_us, uint8_t* data) {
    this->skipping = 0;
    output_row(output_time_us, data, this->width);
}

void EPD::draw_byte(Rect_t* area, short time, uint8_t byte) {

    uint8_t* row = (uint8_t*)malloc(this->width/4);
    for (int i = 0; i < this->width/4; i++) {
        if (i*4 + 3 < area->x || i*4 >= area->x + area->width) {
            row[i] = 0;
        } else {
            // undivisible pixel values
            if (area->x > i*4) {
                row[i] = byte & (0B11111111 >> (2 * (area->x % 4)));
            } else if (i*4 + 4 > area->x + area->width) {
                row[i] = byte & (0B11111111 << (8 - 2 * ((area->x + area->width) % 4)));
            } else {
                row[i] = byte;
            }
        }
    }
    start_frame();
    for (int i = 0; i < this->height; i++) {
        // before are of interest: skip
        if (i < area->y) {
            this->skip_row();
        // start area of interest: set row data
        } else if (i == area->y) {
            this->write_row(time, row);
        // load nop row if done with area
        } else if (i >= area->y + area->height) {
            this->skip_row();
        // output the same as before
        } else {
            this->write_row(time, NULL);
        }
    }
    // Since we "pipeline" row output, we still have to latch out the last row.
    this->write_row(time, NULL);

    end_frame();
    free(row);

}

void EPD::clear_area(Rect_t area) {
    const short white_time = 80;
    const short dark_time = 40;

    for (int i=0; i<8; i++) {
        draw_byte(&area, white_time, CLEAR_BYTE);
    }
    for (int i=0; i<6; i++) {
        draw_byte(&area, dark_time, DARK_BYTE);
    }
    for (int i=0; i<8; i++) {
        draw_byte(&area, white_time, CLEAR_BYTE);
    }
}

Rect_t EPD::full_screen() {
    Rect_t full_screen = { .x = 0, .y = 0, .width = this->width, .height = this->height };
    return full_screen;
}

void EPD::clear_screen() {
    clear_area(this->full_screen());
}

/* shift row bitwise by bits to the right.
 * only touch bytes start to end (inclusive).
 * insert zeroes where gaps are created.
 * information of the end byte is lost.
 *
 * Possible improvement: use larger chunks.
 * */
void shift_row_r(uint8_t* row, uint8_t bits, uint16_t start, uint16_t end) {
    uint8_t carry = 0;
    uint8_t mask = ~(0B11111111 << bits);
    for (uint16_t i=end; i>=start; i--) {
        carry = (row[i - 1] & mask) << (8 - bits);
        row[i] = row[i] >> bits | carry;
    }
}

void EPD::draw_picture(Rect_t area, uint8_t* data) {
    uint8_t* row = (uint8_t*)malloc(this->width/4);
    uint32_t* line = (uint32_t*)malloc(this->width);

    for (uint8_t k = 15; k > 0; k--) {
        uint32_t* ptr = (uint32_t*)data;
        yield();
        start_frame();
        // initialize with null row to avoid artifacts
        for (int i = 0; i < this->height; i++) {
            if (i < area.y || i >= area.y + area.height) {
                this->skip_row();
                continue;
            }

            //uint32_t aligned_end = 4 * (area.x / 4) + area.width;
            uint8_t pixel = 0B00000000;
            memcpy(line, ptr, this->width);
            ptr+=this->width/4;
            uint32_t* lp = line;
            uint8_t displacement_map[4] = {
                2, 3, 0, 1
            };

            volatile uint32_t t = micros();
            for (uint32_t j = 0; j < this->width/4; j++) {
                /*if (j >= area.x && j < area.x + area.width) {
                    uint8_t value = *(ptr++);
                    pixel |= ((value >> 4) < k);
                }*/
                uint32_t val = *(lp++);
                pixel = (val & 0x000000F0) < (k << 4);
                pixel = pixel << 2;
                val = val >> 8;
                pixel |= (val & 0x000000F0) < (k << 4);
                pixel = pixel << 2;
                val = val >> 8;
                pixel |= (val & 0x000000F0) < (k << 4);
                pixel = pixel << 2;
                val = val >> 8;
                pixel |= (val & 0x000000F0) < (k << 4);
                row[(j & ~0x00000003) + displacement_map[j % 4]] = pixel;
            }
            volatile uint32_t t2 = micros();
            //printf("row calc took %d us.\n", t2 - t);
            this->write_row(contrast_cycles[15 - k], row);
        }
        // Since we "pipeline" row output, we still have to latch out the last row.
        this->write_row(contrast_cycles[15 - k], NULL);
        end_frame();
    }
    free(row);
    free(line);
}
