#include "Arduino.h"
#include "ed097oc4.hpp"

inline void gpio_set_hi(gpio_num_t gpio_num)
{
        digitalWrite(gpio_num, HIGH);
}


inline void gpio_set_lo(gpio_num_t gpio_num)
{
        digitalWrite(gpio_num, LOW);
}

/*
Write bits directly using the registers.  Won't work for some signals
(>= 32). May be too fast for some signals.
*/
inline void fast_gpio_set_hi(gpio_num_t gpio_num)
{
    GPIO.out_w1ts = (1 << gpio_num);
}


inline void fast_gpio_set_lo(gpio_num_t gpio_num)
{

    GPIO.out_w1tc = (1 << gpio_num);
}

/*
 * Pulses the horizontal clock to advance the horizontal latch 2 pixels.
 */
inline void next_pixel() {
    fast_gpio_set_hi(CKH);
    fast_gpio_set_lo(CKH);
}


/*
Output data to the GPIOs connected to the Data pins of the ePaper
display.  These will be shifted into pixel positions to be drawn (or
NOP'd) to the screen
*/
inline void data_output(uint8_t data)
{
    // we use the fact that D7 to D2 use adjacent GPIOS
    uint32_t gpio_byte = (data & 0B11111100) << (D7 - 7);
    gpio_byte |= (data & 0B00000011) << (D1 - 1);

    GPIO.out_w1ts = gpio_byte;
    GPIO.out_w1tc = ~gpio_byte & DATA_GPIO_MASK;

    /*
    (data & 128) ? fast_gpio_set_hi(D7) : fast_gpio_set_lo(D7);
    (data & 64) ? fast_gpio_set_hi(D6) : fast_gpio_set_lo(D6);
    (data & 32) ? fast_gpio_set_hi(D5) : fast_gpio_set_lo(D5);
    (data & 16) ? fast_gpio_set_hi(D4) : fast_gpio_set_lo(D4);
    (data & 8) ? fast_gpio_set_hi(D3) : fast_gpio_set_lo(D3);
    (data & 4) ? fast_gpio_set_hi(D2) : fast_gpio_set_lo(D2);
    (data & 2) ? fast_gpio_set_hi(D1) : fast_gpio_set_lo(D1);
    (data & 1) ? fast_gpio_set_hi(D0) : fast_gpio_set_lo(D0);
    */
}

void init_gpios() {

    /* Power Control Output/Off */
    pinMode(POS_CTRL, OUTPUT);
    digitalWrite(POS_CTRL, LOW);
    pinMode(NEG_CTRL, OUTPUT);
    digitalWrite(NEG_CTRL, LOW);
    pinMode(SMPS_CTRL, OUTPUT);
    digitalWrite(SMPS_CTRL, HIGH);

    /* Edges/Clocks */
    pinMode(CKH, OUTPUT);
    digitalWrite(CKH, LOW);
    pinMode(LEH, OUTPUT);
    digitalWrite(LEH, LOW);

    /* Control Lines */
    pinMode(MODE, OUTPUT);
    digitalWrite(MODE, LOW);
    pinMode(STH, OUTPUT);
    digitalWrite(STH, LOW);
    pinMode(CKV, OUTPUT);
    digitalWrite(CKV, LOW);
    pinMode(STV, OUTPUT);
    digitalWrite(STV, LOW);
    pinMode(OEH, OUTPUT);
    digitalWrite(OEH, LOW);

    /* Data Lines */
    pinMode(D7, OUTPUT);
    digitalWrite(D7, LOW);
    pinMode(D6, OUTPUT);
    digitalWrite(D6, LOW);
    pinMode(D5, OUTPUT);
    digitalWrite(D5, LOW);
    pinMode(D4, OUTPUT);
    digitalWrite(D4, LOW);
    pinMode(D3, OUTPUT);
    digitalWrite(D3, LOW);
    pinMode(D2, OUTPUT);
    digitalWrite(D2, LOW);
    pinMode(D1, OUTPUT);
    digitalWrite(D1, LOW);
    pinMode(D0, OUTPUT);
    digitalWrite(D0, LOW);
}

void epd_poweron() {
    // POWERON
    gpio_set_lo(SMPS_CTRL);
    delayMicroseconds(100);
    gpio_set_hi(NEG_CTRL);
    delayMicroseconds(500);
    gpio_set_hi(POS_CTRL);
    delayMicroseconds(100);
    gpio_set_hi(STV);
    gpio_set_hi(STH);
    // END POWERON
}

void epd_poweroff() {
    // POWEROFF
    gpio_set_lo(POS_CTRL);
    delayMicroseconds(10);
    gpio_set_lo(NEG_CTRL);
    delayMicroseconds(100);
    gpio_set_hi(SMPS_CTRL);
    // END POWEROFF
}

void start_frame() {
    // VSCANSTART
    gpio_set_hi(MODE);
    delayMicroseconds(10);

    gpio_set_hi(STV);
    gpio_set_lo(CKV);
    delayMicroseconds(1);
    gpio_set_hi(CKV);

    gpio_set_lo(STV);
    gpio_set_lo(CKV);
    delayMicroseconds(1);
    gpio_set_hi(CKV);

    gpio_set_hi(STV);
    gpio_set_lo(CKV);
    delayMicroseconds(1);
    gpio_set_hi(CKV);


    gpio_set_hi(OEH);
    // END VSCANSTART
}

inline void latch_row()
{
    gpio_set_hi(LEH);
    gpio_set_hi(CKH);
    gpio_set_lo(CKH);
    gpio_set_hi(CKH);
    gpio_set_lo(CKH);

    gpio_set_lo(LEH);
    gpio_set_hi(CKH);
    gpio_set_lo(CKH);
    gpio_set_hi(CKH);
    gpio_set_lo(CKH);
}


// This needs to be in IRAM, otherwise we get weird delays!
void IRAM_ATTR wait_line(uint32_t output_time_us) {
    taskDISABLE_INTERRUPTS();
    fast_gpio_set_hi(CKV);
    unsigned counts = xthal_get_ccount() + output_time_us * 240;
    while (xthal_get_ccount() < counts) {}
    fast_gpio_set_lo(CKV);
    taskENABLE_INTERRUPTS();
}

void skip(uint16_t width) {
    gpio_set_lo(STH);
    data_output(255);
    for (uint32_t i=0; i < width/4; i++) {
        next_pixel();
    }
    gpio_set_hi(STH);
    gpio_set_hi(CKV);
    unsigned counts = xthal_get_ccount() + 480;
    while (xthal_get_ccount() < counts) {}    gpio_set_lo(CKV);
    gpio_set_lo(CKV);
}

void output_row(uint32_t output_time_us, uint8_t* data, uint16_t width)
{
    if (data != NULL) {
        gpio_set_lo(STH);
        for (uint32_t i=0; i < width/4; i++) {
            data_output(*(data++));
            next_pixel();
        }
        gpio_set_hi(STH);
    }

    gpio_set_hi(CKH);
    gpio_set_lo(CKH);
    gpio_set_hi(CKH);
    gpio_set_lo(CKH);

    latch_row();

    wait_line(output_time_us);
}

void end_frame() {
    gpio_set_lo(OEH);
    gpio_set_lo(MODE);
}

void enable_output() {
    gpio_set_hi(OEH);
}

void disable_output() {
    gpio_set_lo(OEH);
}
