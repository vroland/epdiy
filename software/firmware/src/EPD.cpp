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

EPD::EPD() {
    this->skipping = 0;
    this->null_row = (uint8_t*)malloc(EPD_LINE_BYTES);
    memset(this->null_row, 255, EPD_LINE_BYTES);
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
    // 2, to latch out previously loaded null row
    if (this->skipping < 2) {
        output_row(10, this->null_row);
        // avoid tainting of following rows by
        // allowing residual charge to dissipate
        unsigned counts = xthal_get_ccount() + 50 * 240;
        while (xthal_get_ccount() < counts) {};
    } else {
        skip();
    }
    this->skipping++;
}

void EPD::write_row(uint32_t output_time_us, uint8_t* data) {
    this->skipping = 0;
    output_row(output_time_us, data);
}

void EPD::draw_byte(Rect_t* area, short time, uint8_t byte) {

    uint8_t* row = (uint8_t*)malloc(EPD_LINE_BYTES);
    for (int i = 0; i < EPD_LINE_BYTES; i++) {
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
    for (int i = 0; i < EPD_HEIGHT; i++) {
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
    Rect_t full_screen = { .x = 0, .y = 0, .width = EPD_WIDTH, .height = EPD_HEIGHT };
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
    uint8_t* row = (uint8_t*)malloc(EPD_LINE_BYTES);
    uint32_t* line = (uint32_t*)malloc(EPD_WIDTH);

    for (uint8_t k = 15; k > 0; k--) {
        uint8_t* ptr = data;
        yield();
        start_frame();
        // initialize with null row to avoid artifacts
        for (int i = 0; i < EPD_HEIGHT; i++) {
            if (i < area.y || i >= area.y + area.height) {
                this->skip_row();
                continue;
            }

            uint8_t pixel = 0B00000000;
            if (area.width == EPD_WIDTH) {
                memcpy(line, ptr, EPD_WIDTH);
                ptr+=EPD_WIDTH;
            } else {
                memset(line, 255, EPD_WIDTH);
                uint8_t* buf_start = ((uint8_t*)line) + area.x;
                memcpy(buf_start, ptr, area.width);
                ptr += area.width;
            }
            uint32_t* lp = line;

            volatile uint32_t t = micros();
            for (uint32_t j = 0; j < EPD_WIDTH/4; j++) {
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
                row[j] = pixel;
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
