#pragma once

#include <driver/i2c_master.h>

typedef struct EpdI2cConfig {
    i2c_master_bus_handle_t bus_handle;
} EpdI2cConfig;

typedef struct EpdInitConfig {
    const EpdI2cConfig* i2c;
} EpdInitConfig;
