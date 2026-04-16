#pragma once

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "epd_init_config.h"

typedef struct {
    i2c_port_num_t port;
    gpio_num_t sda_io_num;
    gpio_num_t scl_io_num;
    uint32_t bus_speed_hz;
} epd_board_i2c_bus_config_t;

typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t tps;
    i2c_master_dev_handle_t pca;
    bool owns_bus;
} epd_board_i2c_context_t;

esp_err_t epd_board_i2c_init(
    epd_board_i2c_context_t* ctx,
    const epd_board_i2c_bus_config_t* defaults,
    const EpdInitConfig* init_config,
    bool need_tps,
    bool need_pca
);
void epd_board_i2c_deinit(epd_board_i2c_context_t* ctx);
i2c_master_dev_handle_t epd_board_i2c_current_tps();
