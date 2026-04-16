#include <assert.h>
#include <driver/i2c_master.h>
#include <unity.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "epd_board.h"
#include "epd_display.h"
#include "epdiy.h"

// choose the default demo board depending on the architecture
#ifdef CONFIG_IDF_TARGET_ESP32
#define TEST_BOARD epd_board_v6
#define TEST_I2C_SCL GPIO_NUM_33
#define TEST_I2C_SDA GPIO_NUM_32
#define TEST_I2C_SPEED_HZ 400000
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define TEST_BOARD epd_board_v7
#define TEST_I2C_SCL GPIO_NUM_40
#define TEST_I2C_SDA GPIO_NUM_39
#define TEST_I2C_SPEED_HZ 100000
#endif

static i2c_master_bus_config_t make_test_i2c_bus_config(void) {
    return (i2c_master_bus_config_t){
        .i2c_port = I2C_NUM_0,
        .sda_io_num = TEST_I2C_SDA,
        .scl_io_num = TEST_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
}

TEST_CASE("initialization and deinitialization works", "[epdiy,e2e]") {
    epd_init(&TEST_BOARD, &ED097TC2, EPD_OPTIONS_DEFAULT);

    epd_poweron();
    vTaskDelay(2);
    epd_poweroff();

    epd_deinit();
}

TEST_CASE("re-initialization works", "[epdiy,e2e]") {
    epd_init(&TEST_BOARD, &ED097TC2, EPD_OPTIONS_DEFAULT);

    epd_poweron();
    vTaskDelay(2);
    epd_poweroff();

    epd_deinit();

    int before_init = esp_get_free_internal_heap_size();
    epd_init(&TEST_BOARD, &ED097TC2, EPD_OPTIONS_DEFAULT);

    epd_poweron();
    vTaskDelay(2);
    epd_poweroff();

    epd_deinit();
    int after_init = esp_get_free_internal_heap_size();
    TEST_ASSERT_EQUAL(after_init, before_init);
}

TEST_CASE("initialization with external i2c bus keeps bus alive", "[epdiy][i2c][e2e]") {
    i2c_master_bus_handle_t bus_handle = NULL;
    i2c_master_bus_config_t bus_config = make_test_i2c_bus_config();
    TEST_ASSERT_EQUAL(ESP_OK, i2c_new_master_bus(&bus_config, &bus_handle));

    EpdI2cConfig i2c_config = {
        .bus_handle = bus_handle,
    };
    EpdInitConfig init_config = {
        .i2c = &i2c_config,
    };

    epd_init_with_config(&TEST_BOARD, &ED097TC2, EPD_OPTIONS_DEFAULT, &init_config);

    epd_poweron();
    vTaskDelay(2);
    epd_poweroff();

    epd_deinit();

    TEST_ASSERT_EQUAL(ESP_OK, i2c_del_master_bus(bus_handle));
}

TEST_CASE("initialization with internal i2c bus releases bus on deinit", "[epdiy][i2c][e2e]") {
    i2c_master_bus_handle_t bus_handle = NULL;

    epd_init(&TEST_BOARD, &ED097TC2, EPD_OPTIONS_DEFAULT);

    epd_poweron();
    vTaskDelay(2);
    epd_poweroff();

    epd_deinit();

    i2c_master_bus_config_t bus_config = make_test_i2c_bus_config();
    TEST_ASSERT_EQUAL(ESP_OK, i2c_new_master_bus(&bus_config, &bus_handle));
    TEST_ASSERT_EQUAL(ESP_OK, i2c_del_master_bus(bus_handle));
}
