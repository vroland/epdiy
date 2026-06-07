

#include "pca9555.h"
#include <esp_err.h>
#include <esp_log.h>
#include <stdint.h>
#include <string.h>

#define REG_INPUT_PORT0 0
#define REG_INPUT_PORT1 1

#define REG_OUTPUT_PORT0 2
#define REG_OUTPUT_PORT1 3

#define REG_INVERT_PORT0 4
#define REG_INVERT_PORT1 5

#define REG_CONFIG_PORT0 6
#define REG_CONFIG_PORT1 7

static esp_err_t i2c_master_read_slave(
    i2c_master_dev_handle_t dev, uint8_t* data_rd, size_t size, int reg
) {
    if (size == 0) {
        return ESP_OK;
    }
    uint8_t reg_addr = reg;
    return i2c_master_transmit_receive(dev, &reg_addr, sizeof(reg_addr), data_rd, size, -1);
}

static esp_err_t i2c_master_write_slave(
    i2c_master_dev_handle_t dev, uint8_t ctrl, uint8_t* data_wr, size_t size
) {
    uint8_t write_buffer[3] = { ctrl, 0, 0 };
    if (size > sizeof(write_buffer) - 1) {
        ESP_LOGE("epdiy", "I2C write too large for PCA9555 helper");
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(write_buffer + 1, data_wr, size);
    return i2c_master_transmit(dev, write_buffer, size + 1, -1);
}

static esp_err_t pca9555_write_single(i2c_master_dev_handle_t dev, int reg, uint8_t value) {
    uint8_t w_data[1] = { value };
    return i2c_master_write_slave(dev, reg, w_data, sizeof(w_data));
}

esp_err_t pca9555_set_config(i2c_master_dev_handle_t dev, uint8_t config_value, int high_port) {
    return pca9555_write_single(dev, REG_CONFIG_PORT0 + high_port, config_value);
}

esp_err_t pca9555_set_inversion(i2c_master_dev_handle_t dev, uint8_t config_value, int high_port) {
    return pca9555_write_single(dev, REG_INVERT_PORT0 + high_port, config_value);
}

esp_err_t pca9555_set_value(i2c_master_dev_handle_t dev, uint8_t config_value, int high_port) {
    return pca9555_write_single(dev, REG_OUTPUT_PORT0 + high_port, config_value);
}

uint8_t pca9555_read_input(i2c_master_dev_handle_t dev, int high_port) {
    esp_err_t err;
    uint8_t r_data[1];

    err = i2c_master_read_slave(dev, r_data, 1, REG_INPUT_PORT0 + high_port);
    if (err != ESP_OK) {
        ESP_LOGE("PCA9555", "%s failed", __func__);
        return 0;
    }

    return r_data[0];
}
