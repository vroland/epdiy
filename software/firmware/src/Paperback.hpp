#pragma once
#include "Arduino.h"

/* Control Lines */
#define NEG_CTRL  GPIO_NUM_33  // Active HIGH
#define POS_CTRL  GPIO_NUM_32  // Active HIGH
#define SMPS_CTRL GPIO_NUM_18  // Active LOW

/* Control Lines */
#define CKV       GPIO_NUM_25
#define STV       GPIO_NUM_27
#define MODE     GPIO_NUM_0
#define STH       GPIO_NUM_26
#define OEH        GPIO_NUM_19

/* Edges */
#define CKH        GPIO_NUM_23
#define LEH        GPIO_NUM_2

/* Data Lines */
#define D7        GPIO_NUM_17
#define D6        GPIO_NUM_16
#define D5        GPIO_NUM_15
#define D4        GPIO_NUM_14
#define D3        GPIO_NUM_13
#define D2        GPIO_NUM_12
#define D1        GPIO_NUM_5
#define D0        GPIO_NUM_4

/* V2 Does not Define GMODE and RL */



inline void gpio_set_hi(const gpio_num_t& gpio_num)
{
        digitalWrite(gpio_num, HIGH);
}


inline void gpio_set_lo(const gpio_num_t& gpio_num)
{
        digitalWrite(gpio_num, LOW);
}

/*
Write bits directly using the registers.  Won't work for some signals
(>= 32) and too fast for others (such as horizontal clock for the
 latches.  We use it for data.
*/
inline void fast_gpio_set_hi(const gpio_num_t& gpio_num)
{
    //digitalWrite(gpio_num, HIGH);
    GPIO.out_w1ts = (1 << gpio_num);
}


inline void fast_gpio_set_lo(const gpio_num_t& gpio_num)
{

    //digitalWrite(gpio_num, LOW);
    GPIO.out_w1tc = (1 << gpio_num);
}

/* Flip the horizontal clock HIGH/LOW */
inline void clock_pixel()
{
        fast_gpio_set_hi(CKH);
        //delayMicroseconds(1);
        fast_gpio_set_lo(CKH);
        //delayMicroseconds(1);
}

/*
Output data to the GPIOs connected to the Data pins of the ePaper
display.  These will be shifted into pixel positions to be drawn (or
NOP'd) to the screen
*/
inline void data_output(const uint8_t& data)
{
        (data>>7 & 1 == 1) ? fast_gpio_set_hi(D7) : fast_gpio_set_lo(D7);
        (data>>6 & 1 == 1) ? fast_gpio_set_hi(D6) : fast_gpio_set_lo(D6);
        (data>>5 & 1 == 1) ? fast_gpio_set_hi(D5) : fast_gpio_set_lo(D5);
        (data>>4 & 1 == 1) ? fast_gpio_set_hi(D4) : fast_gpio_set_lo(D4);
        (data>>3 & 1 == 1) ? fast_gpio_set_hi(D3) : fast_gpio_set_lo(D3);
        (data>>2 & 1 == 1) ? fast_gpio_set_hi(D2) : fast_gpio_set_lo(D2);
        (data>>1 & 1 == 1) ? fast_gpio_set_hi(D1) : fast_gpio_set_lo(D1);
        (data>>0 & 1 == 1) ? fast_gpio_set_hi(D0) : fast_gpio_set_lo(D0);
}


/* Convert LIGHT/DARK areas of the image to ePaper write commands */
inline uint8_t pixel_to_epd_cmd(const uint8_t& pixel)
{
        switch (pixel) {
                case 1:
                        return 0B00000010;
                case 0:
                        return 0B00000001;
                default:
                        return 0B00000011;
        }
}

class Paperback
{
public:
        /* Paperback constructor.

        Set up the three delays (usually non-delays), and init all of the
        GPIOs we'll need to flip pins on the ePaper display. */
        Paperback(
              uint32_t clock_delay_in,
              uint32_t vclock_delay_in,
              uint32_t output_delay_in
        );

        /* After shifting a row, output it to the screen. */
        void output_row();

        /*
        After adding pixels to the row, latch it to the ePaper display's
        latches.
        */
        void latch_row();

        /* Turn on and off the High Voltage +22V/-20V and +15V/-15V rails */
        void poweron();
        void poweroff();

        /* Start and stop the vertical write */
        void vscan_start();
        void vscan_end();

        /* Start and stop a horizontal (one line) write */
        void hscan_start();
        void hscan_end();


private:
        /* Constant delays, from the .ino file */
        uint32_t clock_delay;
        uint32_t vclock_delay;
        uint32_t output_delay;

        /* Initialize the GPIOs we need (using 20) */
        void init_gpios();
};
