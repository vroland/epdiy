
#include "tps65185.h"
#include "epd_board_i2c.h"
#include "epd_board.h"
#include "esp_err.h"
#include "esp_log.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static uint8_t i2c_master_read_slave(i2c_master_dev_handle_t dev, int reg) {
    uint8_t r_data[1];

    uint8_t reg_addr = reg;
    ESP_ERROR_CHECK(i2c_master_transmit_receive(dev, &reg_addr, sizeof(reg_addr), r_data, 1, -1));

    return r_data[0];
}

static esp_err_t i2c_master_write_slave(
    i2c_master_dev_handle_t dev, uint8_t ctrl, uint8_t* data_wr, size_t size
) {
    uint8_t write_buffer[3] = { ctrl, 0, 0 };
    if (size > sizeof(write_buffer) - 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(write_buffer + 1, data_wr, size);
    return i2c_master_transmit(dev, write_buffer, size + 1, -1);
}

esp_err_t tps_write_register(i2c_master_dev_handle_t dev, int reg, uint8_t value) {
    uint8_t w_data[1];
    esp_err_t err;

    w_data[0] = value;

    err = i2c_master_write_slave(dev, reg, w_data, 1);
    return err;
}

uint8_t tps_read_register(i2c_master_dev_handle_t dev, int reg) {
    return i2c_master_read_slave(dev, reg);
}

void tps_set_vcom(i2c_master_dev_handle_t dev, unsigned vcom_mV) {
    unsigned val = vcom_mV / 10;
    ESP_ERROR_CHECK(tps_write_register(dev, 4, (val & 0x100) >> 8));
    ESP_ERROR_CHECK(tps_write_register(dev, 3, val & 0xFF));
}

int8_t tps_read_thermistor(i2c_master_dev_handle_t dev) {
    tps_write_register(dev, TPS_REG_TMST1, 0x80);
    int tries = 0;
    while (true) {
        uint8_t val = tps_read_register(dev, TPS_REG_TMST1);
        // temperature conversion done
        if (val & 0x20) {
            break;
        }
        tries++;

        if (tries >= 100) {
            ESP_LOGE("epdiy", "thermistor read timeout!");
            break;
        }
    }
    return (int8_t)tps_read_register(dev, TPS_REG_TMST_VALUE);
}

static i2c_master_dev_handle_t current_tps() {
    i2c_master_dev_handle_t dev = epd_board_i2c_current_tps();
    assert(dev != NULL);
    return dev;
}

void tps_vcom_kickback() {
    printf("VCOM Kickback test\n");
    // pull the WAKEUP pin and the PWRUP pin high to enable all output rails.
    epd_current_board()->measure_vcom(epd_ctrl_state());
    // set the HiZ bit in the VCOM2 register (BIT 5) 0x20
    // this puts the VCOM pin in a high-impedance state.
    // bit 3 & 4 Number of acquisitions that is averaged to a single kick-back V. measurement
    i2c_master_dev_handle_t dev = current_tps();
    tps_write_register(dev, 4, 0x38);
    vTaskDelay(1);

    uint8_t int1reg = tps_read_register(dev, TPS_REG_INT1);
    uint8_t vcomreg = tps_read_register(dev, TPS_REG_VCOM2);
}

void tps_vcom_kickback_start() {
    i2c_master_dev_handle_t dev = current_tps();
    uint8_t int1reg = tps_read_register(dev, TPS_REG_INT1);
    // set the ACQ bit in the VCOM2 register to 1 (BIT 7)
    tps_write_register(dev, TPS_REG_VCOM2, 0xA0);
}

unsigned tps_vcom_kickback_rdy() {
    i2c_master_dev_handle_t dev = current_tps();
    uint8_t int1reg = tps_read_register(dev, TPS_REG_INT1);

    if (int1reg == 0x02) {
        uint8_t lsb = tps_read_register(dev, 3);
        uint8_t msb = tps_read_register(dev, 4);
        int u16Value = (lsb | (msb << 8)) & 0x1ff;
        ESP_LOGI("vcom", "raw value:%d temperature:%d C", u16Value, tps_read_thermistor(dev));
        return u16Value * 10;
    } else {
        return 0;
    }
}

void tps_set_upseq_carta1300() {
    i2c_master_dev_handle_t dev = current_tps();
    tps_write_register(dev, TPS_REG_UPSEQ0, 0xE1);
    tps_write_register(dev, TPS_REG_UPSEQ1, 0xAA);
}
