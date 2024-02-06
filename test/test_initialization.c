#include <unity.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "epd_board.h"
#include "epdiy.h"
#include "epd_display.h"

TEST_CASE("Mean of an empty array is zero", "[mean]")
{
    const int values[] = { 0 };
    TEST_ASSERT_EQUAL(1, 1);
}

TEST_CASE("V7 initialization and deinitialization works", "[epdiy]")
{
    epd_init(&epd_board_v7, &ED097TC2, EPD_OPTIONS_DEFAULT);

    epd_poweron();
    vTaskDelay(2);
    epd_poweroff();

    epd_deinit();
}

TEST_CASE("V7 re-initialization works", "[epdiy]")
{
    epd_init(&epd_board_v7, &ED097TC2, EPD_OPTIONS_DEFAULT);

    epd_poweron();
    vTaskDelay(2);
    epd_poweroff();

    epd_deinit();

    epd_init(&epd_board_v7, &ED097TC2, EPD_OPTIONS_DEFAULT);
 
    epd_poweron();
    vTaskDelay(2);
    epd_poweroff();

    epd_deinit();
}