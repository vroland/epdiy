/*
 * Estimate the optimal VCOM voltage for TPS65185-powered boards (e.g. v6)
 * This is useful for panels with missing VCOM label, which is often the case
 * with the cost-effective screens from online Chinese shops.
 */

#include "esp_log.h"
#include "sdkconfig.h"
#include <string.h>
#include "assert.h"

#include "epd_driver.h"

// NOTE: the inclusion of the following low-level headers breaks EPDiy's
//       "official" interface/implementation separation, but for this
//       application we don't need multiple FreeRTOS tasks, queues for passing
//       data between them etc...
#include "display_ops.h"
#include "i2s_data_bus.h"
#include "tps65185.h"
#include "lut.h"


// "Do nothing"(i.e. neither push-to-black, nor push-to-white) byte pattern
#define NULL_CMD 0x00

// Driving duration (in 1/10 usec) for a single row of pixels. The measured
// VCOM is highly dependent on this value, which was determined by trial and
// error.
#define FRAME_TIME 5135

// Set the number of VCOM measurements to take and calculate the average of.
// The valid values are in the range 0-3, and are interpreted as the power to
// which 2 must be raised to arrive to the actual number of measurements to
// take and average. That is, '0' means 1(no averaging) and '3' means 8
// measurements must be taken and averaged.
#define VCOM_AVG 3

void init_dma_buffers() {
    volatile uint8_t *dma_buf = i2s_get_current_buffer();
    memset((uint8_t*) dma_buf, NULL_CMD, EPD_WIDTH / 4);
    i2s_switch_buffer();
    dma_buf = i2s_get_current_buffer();
    memset((uint8_t*) dma_buf, NULL_CMD, EPD_WIDTH / 4);
    i2s_switch_buffer();
}

// drive the panel with do-nothing waveform
void drive_null_waveform() {
    epd_start_frame();
    for (int i = 0; i < EPD_HEIGHT; i++) {
      write_row(FRAME_TIME);
    }
    epd_end_frame();
}

void IRAM_ATTR app_main() {
    gpio_hold_dis(CKH); // freeing CKH after wakeup
    epd_base_init(EPD_WIDTH);

    init_dma_buffers();

    assert(VCOM_AVG >= 0 && VCOM_AVG <= 3);
    uint8_t avg_count = 1 << VCOM_AVG;
    ESP_LOGI("vcom-calib", "VCOM will be measured %d times", avg_count);

    // implement the procedure described in section "8.3.7.1 Kick-Back Voltage
    // Measurement" in TPS65185's datasheet
    epd_poweron();
    uint8_t vcom2 = tps_read_register(EPDIY_I2C_PORT, TPS_REG_VCOM2);
    vcom2 |= (1 << 5); // set the HiZ bit to put VCOM pin in Z state
    vcom2 |= (VCOM_AVG << 4); // set the AVG field
    tps_write_register(EPDIY_I2C_PORT, TPS_REG_VCOM2, vcom2);
    drive_null_waveform();
    ESP_LOGI("vcom-calib", "Starting VCOM measurements ...");
    vcom2 |= (1 << 7); // set the ACQ flag to start VCOM measurement
    tps_write_register(EPDIY_I2C_PORT, TPS_REG_VCOM2, vcom2);
    for (int32_t i = 0;; ++i) {
        drive_null_waveform();
        uint8_t int1 = tps_read_register(EPDIY_I2C_PORT, TPS_REG_INT1);
        if (int1 & 0x2) { // if ACQC is raised => measurements are complete
            ESP_LOGI("vcom-calib", "Measurement finished after %d null frame(s)", i+1);
            break;
        }
    }
    uint16_t vcom1 = tps_read_register(EPDIY_I2C_PORT, TPS_REG_VCOM1);
    vcom2 = tps_read_register(EPDIY_I2C_PORT, TPS_REG_VCOM2);
    vcom1 |= ((uint16_t) (vcom2 & 0x1)) << 8;
    ESP_LOGI("vcom-calib", "Estimated optimal VCOM: -%d mV", vcom1 * 10);
    epd_poweroff();
}
