
#include "tps65185.h"
#include "esp_err.h"
#include "esp_log.h"

#include <driver/i2c.h>
#include <stdint.h>

static const int EPDIY_TPS_ADDR = 0x68;

static uint8_t i2c_master_read_slave(i2c_port_t i2c_num, int reg)
{
	uint8_t r_data[1];

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, ( EPDIY_TPS_ADDR << 1 ) | I2C_MASTER_WRITE, true);
	i2c_master_write_byte(cmd, reg, true);
	i2c_master_stop(cmd);

    ESP_ERROR_CHECK(i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_PERIOD_MS));
	i2c_cmd_link_delete(cmd);

	cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
	i2c_master_write_byte(cmd, ( EPDIY_TPS_ADDR << 1 ) | I2C_MASTER_READ, true);
    /*
	if (size > 1) {
        i2c_master_read(cmd, data_rd, size - 1, I2C_MASTER_ACK);
    }
    */
    i2c_master_read_byte(cmd, r_data, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

	ESP_ERROR_CHECK(i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_PERIOD_MS));
    i2c_cmd_link_delete(cmd);


    return r_data[0];
}

static esp_err_t i2c_master_write_slave(i2c_port_t i2c_num, uint8_t ctrl,  uint8_t* data_wr, size_t size)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, ( EPDIY_TPS_ADDR  << 1 ) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, ctrl, true);

    i2c_master_write(cmd, data_wr, size, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t tps_write_register(i2c_port_t port, int reg, uint8_t value) {
	uint8_t w_data[1];
    esp_err_t err;

	w_data[0] = value;

    err = i2c_master_write_slave(port, reg, w_data, 1);
    return err;
}


uint8_t tps_read_register(i2c_port_t i2c_num, int reg) {
    return i2c_master_read_slave(i2c_num, reg);
}

void tps_set_vcom(i2c_port_t i2c_num, unsigned vcom_mV) {
    unsigned val = vcom_mV / 10;
    ESP_ERROR_CHECK(tps_write_register(i2c_num, 4, (val & 0x100) >> 8));
    ESP_ERROR_CHECK(tps_write_register(i2c_num, 3, val & 0xFF));
}

int v6_wait_for_interrupt(int timeout);

int8_t tps_read_thermistor(i2c_port_t i2c_num) {
    tps_write_register(i2c_num, TPS_REG_TMST1, 0x80);
    int tries = 0;
    while (true) {
        uint8_t val = tps_read_register(i2c_num, TPS_REG_TMST1);
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
    return (int8_t)tps_read_register(i2c_num, TPS_REG_TMST_VALUE);
}
