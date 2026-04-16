#include "epd_board_i2c.h"

#include "pca9555.h"
#include "epdiy.h"

#include <esp_log.h>

static const uint32_t GLITCH_IGNORE_COUNT = 7;
static i2c_master_dev_handle_t current_tps;

static esp_err_t epd_board_i2c_add_device(
    i2c_master_bus_handle_t bus,
    uint16_t address,
    uint32_t bus_speed_hz,
    i2c_master_dev_handle_t* out_dev
) {
    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = bus_speed_hz,
    };
    return i2c_master_bus_add_device(bus, &device_config, out_dev);
}

esp_err_t epd_board_i2c_init(
    epd_board_i2c_context_t* ctx,
    const epd_board_i2c_bus_config_t* defaults,
    const EpdInitConfig* init_config,
    bool need_tps,
    bool need_pca
) {
    *ctx = (epd_board_i2c_context_t){ 0 };

    if (init_config && init_config->i2c && init_config->i2c->bus_handle) {
        ctx->bus = init_config->i2c->bus_handle;
        ctx->owns_bus = false;
    } else {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = defaults->port,
            .sda_io_num = defaults->sda_io_num,
            .scl_io_num = defaults->scl_io_num,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = GLITCH_IGNORE_COUNT,
            .flags.enable_internal_pullup = true,
        };
        esp_err_t err = i2c_new_master_bus(&bus_config, &ctx->bus);
        if (err != ESP_OK) {
            ESP_LOGE("epdiy", "failed to create I2C bus");
            return err;
        }
        ctx->owns_bus = true;
    }

    if (need_tps) {
        esp_err_t err = epd_board_i2c_add_device(ctx->bus, 0x68, defaults->bus_speed_hz, &ctx->tps);
        if (err != ESP_OK) {
            epd_board_i2c_deinit(ctx);
            return err;
        }
        current_tps = ctx->tps;
    }

    if (need_pca) {
        esp_err_t err = epd_board_i2c_add_device(
            ctx->bus, EPDIY_PCA9555_ADDR, defaults->bus_speed_hz, &ctx->pca
        );
        if (err != ESP_OK) {
            epd_board_i2c_deinit(ctx);
            return err;
        }
    }

    return ESP_OK;
}

void epd_board_i2c_deinit(epd_board_i2c_context_t* ctx) {
    if (ctx->pca) {
        i2c_master_bus_rm_device(ctx->pca);
    }
    if (ctx->tps) {
        if (current_tps == ctx->tps) {
            current_tps = NULL;
        }
        i2c_master_bus_rm_device(ctx->tps);
    }
    if (ctx->owns_bus && ctx->bus) {
        i2c_del_master_bus(ctx->bus);
    }
    *ctx = (epd_board_i2c_context_t){ 0 };
}

i2c_master_dev_handle_t epd_board_i2c_current_tps() {
    return current_tps;
}
