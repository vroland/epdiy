#pragma once

#include "display_ops.h"
#include "pca9555.h"
#include "tps65185.h"
#include <driver/i2c.h>

#define CFG_PIN_OE PCA_PIN_PC10
#define CFG_PIN_MODE PCA_PIN_PC11
#define CFG_PIN_STV PCA_PIN_PC12
#define CFG_PIN_PWRUP PCA_PIN_PC13
#define CFG_PIN_VCOM_CTRL PCA_PIN_PC14
#define CFG_PIN_WAKEUP PCA_PIN_PC15
#define CFG_PIN_PWRGOOD PCA_PIN_PC16
#define CFG_PIN_INT PCA_PIN_PC17

typedef struct {
    i2c_port_t port;
    bool ep_output_enable;
    bool ep_mode;
    bool ep_stv;
    bool pwrup;
    bool vcom_ctrl;
    bool wakeup;
    bool others[8];
} epd_config_register_t;

void config_reg_init(epd_config_register_t* reg) {
    reg->ep_output_enable = false;
    reg->ep_mode = false;
    reg->ep_stv = false;
    reg->pwrup = false;
    reg->vcom_ctrl = false;
    reg->wakeup = false;
    for (int i=0; i<8; i++) {
        reg->others[i] = false;
    }


    // set all epdiy lines to output except TPS interrupt + PWR good
    ESP_ERROR_CHECK(pca9555_set_config(reg->port, CFG_PIN_PWRGOOD | CFG_PIN_INT | PCA_PIN_P_ALL));
}

static void push_cfg(epd_config_register_t* reg) {
    uint16_t value = 0x00;
    if (reg->ep_output_enable) value |= CFG_PIN_OE;
    if (reg->ep_mode) value |= CFG_PIN_MODE;
    if (reg->ep_stv) value |= CFG_PIN_STV;
    if (reg->pwrup) value |= CFG_PIN_PWRUP;
    if (reg->vcom_ctrl) value |= CFG_PIN_VCOM_CTRL;
    if (reg->wakeup) value |= CFG_PIN_WAKEUP;
    for (int i=0; i<8; i++) {
        if (reg->others[i]) {
            value |= (PCA_PIN_P00 << i);
        }
    }

    ESP_ERROR_CHECK(pca9555_set_value(reg->port, value));
}

static void cfg_poweron(epd_config_register_t* reg) {
    reg->ep_stv = true;
    reg->wakeup = true;
    reg->pwrup = true;
    push_cfg(reg);
    reg->vcom_ctrl = true;
    push_cfg(reg);
    while (!(pca9555_read_input(reg->port) & CFG_PIN_PWRGOOD)) {
        vTaskDelay(1);
    }

    ESP_ERROR_CHECK(tps_write_register(reg->port, TPS_REG_ENABLE, 0x3F));
    ESP_ERROR_CHECK(tps_write_register(reg->port, TPS_REG_INT_EN1, 0x7F));
    ESP_ERROR_CHECK(tps_write_register(reg->port, TPS_REG_INT_EN1, 0xFF));

	printf("tps enable: %02X\n", tps_read_register(I2C_NUM_0, TPS_REG_ENABLE));
	printf("tps adj: %02X\n", tps_read_register(I2C_NUM_0, TPS_REG_VADJ));
	printf("tps pg: %02X\n", tps_read_register(I2C_NUM_0, TPS_REG_PG));
	printf("tps revid: %02X\n", tps_read_register(I2C_NUM_0, TPS_REG_REVID));
	printf("tps thermistor: %d\n", tps_read_thermistor(I2C_NUM_0));

    tps_set_vcom(reg->port, 1560);
    gpio_set_level(STH, 1);
    ESP_LOGI("v6", "power on complete!");

    vTaskDelay(100);

	printf("tps int1: %02X\n", tps_read_register(I2C_NUM_0, TPS_REG_INT1));
	printf("tps int2: %02X\n", tps_read_register(I2C_NUM_0, TPS_REG_INT2));
}

static void cfg_poweroff(epd_config_register_t* reg) {
	printf("tps int1: %02X\n", tps_read_register(I2C_NUM_0, TPS_REG_INT1));
	printf("tps int2: %02X\n", tps_read_register(I2C_NUM_0, TPS_REG_INT2));
    reg->vcom_ctrl = false;
    push_cfg(reg);
    reg->wakeup = false;
    reg->pwrup = false;
    reg->ep_stv = false;
    push_cfg(reg);
}
