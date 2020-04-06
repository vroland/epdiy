/**
 * Emit a pulse of precise length on a pin, using the RMT peripheral.
 */

#pragma once
#include "driver/gpio.h"

/**
 * Initializes RMT Channel 0 with a pin for RMT pulsing.
 * The pin will have to be re-initialized if subsequently used as GPIO.
 */
void rmt_pulse_init(gpio_num_t pin);

