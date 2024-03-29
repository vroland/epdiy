#include <unity.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "epd_board.h"
#include "epdiy.h"
#include "epd_display.h"


// choose the default demo board depending on the architecture
#ifdef CONFIG_IDF_TARGET_ESP32
#define TEST_BOARD epd_board_v6
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define TEST_BOARD epd_board_v7
#endif

TEST_CASE("initialization and deinitialization works", "[epdiy,e2e]")
{
    epd_init(&TEST_BOARD, &ED097TC2, EPD_OPTIONS_DEFAULT);

    epd_poweron();
    vTaskDelay(2);
    epd_poweroff();

    epd_deinit();
}

TEST_CASE("re-initialization works", "[epdiy,e2e]")
{
    epd_init(&TEST_BOARD, &ED097TC2, EPD_OPTIONS_DEFAULT);

    epd_poweron();
    vTaskDelay(2);
    epd_poweroff();

    epd_deinit();

    epd_init(&TEST_BOARD, &ED097TC2, EPD_OPTIONS_DEFAULT);
 
    epd_poweron();
    vTaskDelay(2);
    epd_poweroff();

    epd_deinit();
}