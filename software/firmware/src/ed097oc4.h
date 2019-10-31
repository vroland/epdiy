#pragma once
#include "Arduino.h"

// flag is reset by a timer interrupt to allow precise timing of pulses
volatile uint32_t next_row_clear = 0;

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

/* Control Lines */
#define NEG_CTRL  GPIO_NUM_33  // Active HIGH
#define POS_CTRL  GPIO_NUM_32  // Active HIGH
#define SMPS_CTRL GPIO_NUM_18  // Active LOW

/* Control Lines */
#define CKV       GPIO_NUM_25
#define STV       GPIO_NUM_27
#define MODE      GPIO_NUM_0
#define STH       GPIO_NUM_26
#define OEH       GPIO_NUM_19

/* Edges */
#define CKH       GPIO_NUM_23
#define LEH       GPIO_NUM_2

/* Data Lines */
#define D7        GPIO_NUM_17
#define D6        GPIO_NUM_16
#define D5        GPIO_NUM_15
#define D4        GPIO_NUM_14
#define D3        GPIO_NUM_13
#define D2        GPIO_NUM_12
#define D1        GPIO_NUM_5
#define D0        GPIO_NUM_4

#define CLK_DELAY_US  0
#define VCLK_DELAY_US 0

void IRAM_ATTR on_output_timer_expire() {

    //portENTER_CRITICAL_ISR(&timerMux);
    //GPIO.out_w1tc = (1 << OEH);
    //GPIO.out_w1tc = (1 << CKV);
    //next_row_clear = 0;
    //portEXIT_CRITICAL_ISR(&timerMux);
}

/* Convert LIGHT/DARK areas of the image to ePaper write commands */
inline uint8_t pixel_to_epd_cmd(uint8_t pixel)
{
    return 1 << pixel;
}

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
(>= 32) and too fast for others (such as horizontal clock for the
 latches.  We use it for data.
*/
inline void fast_gpio_set_hi(gpio_num_t gpio_num)
{
    //digitalWrite(gpio_num, HIGH);
    GPIO.out_w1ts = (1 << gpio_num);
}


inline void fast_gpio_set_lo(gpio_num_t gpio_num)
{

    //digitalWrite(gpio_num, LOW);
    GPIO.out_w1tc = (1 << gpio_num);
}

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
        (data & 128) ? fast_gpio_set_hi(D7) : fast_gpio_set_lo(D7);
        (data & 64) ? fast_gpio_set_hi(D6) : fast_gpio_set_lo(D6);
        (data & 32) ? fast_gpio_set_hi(D5) : fast_gpio_set_lo(D5);
        (data & 16) ? fast_gpio_set_hi(D4) : fast_gpio_set_lo(D4);
        (data & 8) ? fast_gpio_set_hi(D3) : fast_gpio_set_lo(D3);
        (data & 4) ? fast_gpio_set_hi(D2) : fast_gpio_set_lo(D2);
        (data & 2) ? fast_gpio_set_hi(D1) : fast_gpio_set_lo(D1);
        (data & 1) ? fast_gpio_set_hi(D0) : fast_gpio_set_lo(D0);
}


inline void init_gpios() {

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

inline void poweron() {
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

inline void poweroff() {
    // POWEROFF
    gpio_set_lo(POS_CTRL);
    delayMicroseconds(10);
    gpio_set_lo(NEG_CTRL);
    delayMicroseconds(100);
    gpio_set_hi(SMPS_CTRL);
    // END POWEROFF
}

inline void start_frame() {
    // VSCANSTART
    gpio_set_hi(MODE);
    delayMicroseconds(10);

    gpio_set_hi(STV);
    gpio_set_lo(CKV);
    gpio_set_hi(CKV);

    gpio_set_lo(STV);
    gpio_set_lo(CKV);
    gpio_set_hi(CKV);

    gpio_set_hi(STV);
    gpio_set_lo(CKV);
    gpio_set_hi(CKV);


    gpio_set_hi(OEH);
    // END VSCANSTART
}




inline void start_line() {
    // HSCANSTART
    //gpio_set_hi(OEH);
    // END HSCANSTART
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


inline void fill_byte(uint8_t byte) {
    data_output(byte);
    gpio_set_lo(STH);
    for (uint32_t i=0; i < 300; i++) {
        next_pixel();
    }
    gpio_set_hi(STH);
}

// This needs to be in IRAM, otherwise we get weired delays!
void IRAM_ATTR output_row(uint32_t output_time_us, uint8_t* data, hw_timer_t* timer)
{
    //while (next_row_clear) {};
    //portENTER_CRITICAL(&timerMux);

    //fast_gpio_set_hi(OEH);

    // for some reason, we must immediately begin writing data,
    // otherwise the we introduce weird delays
    gpio_set_lo(STH);
    if (data != NULL) {
        for (uint32_t i=0; i < 300; i++) {
            data_output(*(data++));
            next_pixel();
        }
    }
    gpio_set_hi(STH);

    //gpio_set_hi(CKH);
    //gpio_set_lo(CKH);
    //gpio_set_hi(CKH);
    //gpio_set_lo(CKH);


    latch_row();
    //next_row_clear = 1;


    taskDISABLE_INTERRUPTS();
    fast_gpio_set_hi(CKV);
    for (uint32_t i=0; i < output_time_us * 40; i++) {
        asm volatile ("nop");
    }
    fast_gpio_set_lo(CKV);
    //for (uint32_t i=0; i < 30; i++) {
    //    __asm__("nop");
    //}
    //fast_gpio_set_lo(OEH);
    //timerStop(timer);
    //timerWrite(timer, 0);
    //timerAlarmWrite(timer, output_time_us * 5, false);
    //timerAlarmEnable(timer);
    //timerStart(timer);
    //fast_gpio_set_hi(OEH);
    // NEXTROW START
    //
    //gpio_set_hi(CKH);
    //gpio_set_lo(CKH);
    //gpio_set_hi(CKH);
    //gpio_set_lo(CKH);
    //portEXIT_CRITICAL(&timerMux);
    taskENABLE_INTERRUPTS();

    // END NEXTROW
}


inline void end_frame(hw_timer_t* timer) {
    // VSCANEND
    //while (next_row_clear) {};
    //fill_byte(0);
    //gpio_set_hi(CKV);
    //gpio_set_lo(CKV);
    //gpio_set_hi(CKV);
    //gpio_set_lo(CKV);

    //next_pixel();
    //next_pixel();

    //delayMicroseconds(1);
    //gpio_set_hi(CKV);
    //delayMicroseconds(50);
    //gpio_set_lo(CKV);
    //delayMicroseconds(1);
    gpio_set_lo(OEH);
    gpio_set_lo(MODE);
    //gpio_set_hi(CKV);
    //gpio_set_lo(CKV);
    //
    // END VSCANEND
}
