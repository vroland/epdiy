#pragma once

#include "display_ops.h"
#include "pca9555.h"
#include "tps65185.h"
#include <driver/i2c.h>
#include <driver/gpio.h>
#include <sys/time.h>

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

static bool interrupt_done = false;

static void IRAM_ATTR interrupt_handler(void* arg) {
    interrupt_done = true;
}

int v6_wait_for_interrupt(int timeout) {
    struct timeval start;
    struct timeval now;
    gettimeofday(&start, DST_NONE);
    while (!interrupt_done && gpio_get_level(CFG_INTR) == 1) {
        gettimeofday(&now, DST_NONE);
        if ((((now.tv_sec - start.tv_sec) * 1000000L
            + now.tv_usec) - start.tv_sec) / 1000 > timeout) {
            return -1;
        }
        vTaskDelay(1);
    }
    int ival = 0;
    interrupt_done = false;
    pca9555_read_input(I2C_NUM_0);
	ival = tps_read_register(I2C_NUM_0, TPS_REG_INT1);
	ival |= tps_read_register(I2C_NUM_0, TPS_REG_INT2) << 8;
    while (!gpio_get_level(CFG_INTR)) {vTaskDelay(1); }
    return ival;
}

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

    gpio_set_direction(CFG_INTR, GPIO_MODE_INPUT);
    gpio_set_intr_type(CFG_INTR, GPIO_INTR_NEGEDGE);

    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_EDGE));

    ESP_ERROR_CHECK(gpio_isr_handler_add(CFG_INTR, interrupt_handler, (void *) CFG_INTR));

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

    tps_set_vcom(reg->port, CONFIG_EPD_DRIVER_V6_VCOM);

    gpio_set_level(STH, 1);

    int tries = 0;
    while (!((tps_read_register(I2C_NUM_0, TPS_REG_PG) & 0xFA) == 0xFA)) {
        if (tries >= 1000) {
            ESP_LOGE("epdiy", "Power enable failed! PG status: %X", tps_read_register(I2C_NUM_0, TPS_REG_PG));
            return;
        }
        tries++;
        vTaskDelay(1);
    }
    vTaskDelay(10);
}

static void cfg_poweroff(epd_config_register_t* reg) {
    reg->vcom_ctrl = false;
    push_cfg(reg);
    reg->wakeup = false;
    reg->pwrup = false;
    reg->ep_stv = false;
    push_cfg(reg);
}
