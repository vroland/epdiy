#include "Paperback.hpp"

/*
See the header file for comments.  Generally, these are the signals and
orders needed to get the ePaper Display to react to our input.

Others are utility functions to easily write the image to the screen.
*/



Paperback::Paperback(
      uint32_t clock_delay_in,
      uint32_t vclock_delay_in,
      uint32_t output_delay_in
)
      : clock_delay(clock_delay_in)
      , vclock_delay(vclock_delay_in)
      , output_delay(output_delay_in)
{
      init_gpios();
}


void Paperback::init_gpios()
{
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


void Paperback::output_row()
{
        // OUTPUTROW
        delayMicroseconds(output_delay);
        gpio_set_hi(OEH);
        gpio_set_hi(CKV);
        delayMicroseconds(output_delay);
        gpio_set_lo(CKV);
        delayMicroseconds(output_delay);
        gpio_set_lo(OEH);
        // END OUTPUTROW

        // NEXTROW START
        gpio_set_hi(CKH);
        delayMicroseconds(clock_delay);
        gpio_set_lo(CKH);
        delayMicroseconds(clock_delay);
        gpio_set_hi(CKH);
        delayMicroseconds(clock_delay);
        gpio_set_lo(CKH);
        delayMicroseconds(clock_delay);
        gpio_set_hi(CKV);
        // END NEXTROW
}


void Paperback::latch_row()
{
        gpio_set_hi(LEH);
        delayMicroseconds(clock_delay);
        gpio_set_hi(CKH);
        delayMicroseconds(clock_delay);
        gpio_set_lo(CKH);
        delayMicroseconds(clock_delay);
        gpio_set_hi(CKH);
        delayMicroseconds(clock_delay);
        gpio_set_lo(CKH);
        delayMicroseconds(clock_delay);

        gpio_set_lo(LEH);
        delayMicroseconds(clock_delay);
        gpio_set_hi(CKH);
        delayMicroseconds(clock_delay);
        gpio_set_lo(CKH);
        delayMicroseconds(clock_delay);
        gpio_set_hi(CKH);
        delayMicroseconds(clock_delay);
        gpio_set_lo(CKH);
        delayMicroseconds(clock_delay);
}


void Paperback::poweron()
{
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


void Paperback::vscan_start()
{
        // VSCANSTART
        gpio_set_hi(MODE);
        //gpio_set_hi(OEH);
        delayMicroseconds(500);

        gpio_set_hi(STV);
        delayMicroseconds(vclock_delay);
        gpio_set_lo(CKV);
        delayMicroseconds(vclock_delay);
        gpio_set_hi(CKV);
        delayMicroseconds(vclock_delay);

        gpio_set_lo(STV);
        delayMicroseconds(vclock_delay);
        gpio_set_lo(CKV);
        delayMicroseconds(vclock_delay);
        gpio_set_hi(CKV);
        delayMicroseconds(vclock_delay);

        gpio_set_hi(STV);
        delayMicroseconds(vclock_delay);
        gpio_set_lo(CKV);
        delayMicroseconds(vclock_delay);
        gpio_set_hi(CKV);
        delayMicroseconds(vclock_delay);
        // END VSCANSTART
}


void Paperback::vscan_end()
{
        // VSCANEND
        data_output(0B00000000);
        hscan_start();
        for (int j = 0; j < 300; ++j) {
                gpio_set_hi(CKH);
                delayMicroseconds(clock_delay);
                gpio_set_lo(CKH);
                delayMicroseconds(clock_delay);
        }
        hscan_end();
        noInterrupts();
        gpio_set_hi(OEH);
        gpio_set_hi(CKH);
        delayMicroseconds(1);
        gpio_set_lo(CKV);
        delayMicroseconds(1);
        gpio_set_lo(OEH);
        interrupts();
        gpio_set_hi(CKV);
        delayMicroseconds(clock_delay);
        gpio_set_lo(CKV);
        delayMicroseconds(clock_delay);
        gpio_set_hi(CKV);
        delayMicroseconds(clock_delay);
        gpio_set_lo(CKV);
        delayMicroseconds(clock_delay);
        delayMicroseconds(1);
        gpio_set_lo(CKV);
        gpio_set_lo(OEH);
        delayMicroseconds(50);
        gpio_set_hi(CKV);
        delayMicroseconds(50);
        gpio_set_lo(CKV);
        delayMicroseconds(1);
        gpio_set_lo(MODE);
        delayMicroseconds(1);
        // END VSCANEND
}


void Paperback::hscan_start()
{
        // HSCANSTART
        gpio_set_hi(OEH);
        gpio_set_lo(STH);
        // END HSCANSTART
}


void Paperback::hscan_end()
{
        // HSCANEND
        gpio_set_hi(STH);
        delayMicroseconds(clock_delay);
        gpio_set_hi(CKH);
        delayMicroseconds(clock_delay);
        gpio_set_lo(CKH);
        delayMicroseconds(clock_delay);
        gpio_set_hi(CKH);
        delayMicroseconds(clock_delay);
        gpio_set_lo(CKH);
        delayMicroseconds(clock_delay);
}


void Paperback::poweroff()
{
        // POWEROFF
        gpio_set_lo(POS_CTRL);
        delayMicroseconds(10);
        gpio_set_lo(NEG_CTRL);
        delayMicroseconds(100);
        gpio_set_hi(SMPS_CTRL);
        // END POWEROFF
}









