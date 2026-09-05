#pragma once

// Milliseconds, independent of the FreeRTOS tick rate. A stalled transaction
// must not defeat the surrounding power-good deadline.
#define EPD_POWER_I2C_TIMEOUT_MS 50
#define EPD_POWER_GOOD_TIMEOUT_MS 500
#define EPD_POWER_POLL_INTERVAL_MS 10
