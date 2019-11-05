#include "EPD.hpp"
#include "ed097oc4.hpp"

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

    for (uint8_t k = 15; k > 0; k--) {
        uint8_t* ptr = data;
        yield();
        start_frame();
        // initialize with null row to avoid artifacts
        for (int i = 0; i < this->height; i++) {
            if (i < area.y || i >= area.y + area.height) {
                this->skip_row();
                continue;
            }

            uint32_t aligned_end = 4 * (area.x / 4) + area.width;
            for (uint32_t j = 0; j < this->width/4; j++) {
                uint8_t pixel = 0B00000000;
                if (j * 4 + 3 >= area.x && j * 4 < area.x + area.width) {
                    uint8_t value = *(ptr++);
                    pixel |= ((value >> 4) < k) << 6;
                    pixel |= ((value & 0B00001111) < k) << 4;
                    if (j * 4 + 3 <= aligned_end) {
                        uint8_t value = *(ptr++);
                        pixel |= ((value >> 4) < k) << 2;
                        pixel |= ((value & 0B00001111) < k) << 0;
                    }
                }
                row[j] = pixel;
            }
            uint8_t offset = area.width % 4;
            if (aligned_end < this->width / 4 && offset) {
                row[area.x / 4 + area.width / 4] &= 0B11111111 ^ (0B00000011 << (6 - 2 * offset));
            }
            // image is not aligned
            if (area.x % 4 > 0) {
                shift_row_r(row, 2 * (area.x % 4), 1, this->width / 4 - 1);
            }
            this->write_row(contrast_cycles[15 - k], row);
        }
        end_frame();
    }
    free(row);
}
