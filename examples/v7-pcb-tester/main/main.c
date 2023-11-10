/* Simple firmware to turn HIGH and Low sequencially the Data and Signals for controlling epaper
 * displays */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "epdiy.h"

#include <driver/gpio.h>

#define D15 GPIO_NUM_47
#define D14 GPIO_NUM_21
#define D13 GPIO_NUM_14
#define D12 GPIO_NUM_13
#define D11 GPIO_NUM_12
#define D10 GPIO_NUM_11
#define D9 GPIO_NUM_10
#define D8 GPIO_NUM_9

#define D7 GPIO_NUM_8
#define D6 GPIO_NUM_18
#define D5 GPIO_NUM_17
#define D4 GPIO_NUM_16
#define D3 GPIO_NUM_15
#define D2 GPIO_NUM_7
#define D1 GPIO_NUM_6
#define D0 GPIO_NUM_5

/* Control Lines */
#define CKV GPIO_NUM_48
#define STH GPIO_NUM_41
#define LEH GPIO_NUM_42
#define STV GPIO_NUM_45
/* Edges */
#define CKH GPIO_NUM_4

gpio_num_t data_bus[] = {D0, D1, D2, D3, D4, D5, D6, D7, D8, D9, D10, D11, D12, D13, D14, D15};

gpio_num_t signal_bus[] = {CKV, STH, LEH, STV, CKH};

void idf_loop() {
    // Sequence data GPIOs
    for (int x = 0; x < 16; x++) {
        printf("IO:%d\n", (int)data_bus[x]);
        gpio_set_level(data_bus[x], 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(data_bus[x], 0);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelay(pdMS_TO_TICKS(800));

    // Sequence signal GPIOs
    for (int x = 0; x < 5; x++) {
        gpio_set_level(signal_bus[x], 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(signal_bus[x], 0);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelay(pdMS_TO_TICKS(800));
}

void idf_setup() {
    // If we do not instance epdiy then we should declare all the Data and signals as OUTPUT
    epd_init(&epd_board_v7, &ED078KC1, EPD_LUT_64K);

    epd_set_vcom(1560);

    // Sequence data GPIOs
    // Initialize GPIOs direction & initial states
    printf("Set all IOs output\n\n");
    for (int x = 0; x < 16; x++) {
        gpio_set_direction(data_bus[x], GPIO_MODE_OUTPUT);
    }
    for (int x = 0; x < 5; x++) {
        gpio_set_direction(signal_bus[x], GPIO_MODE_OUTPUT);
    }
}

#ifndef ARDUINO_ARCH_ESP32
void app_main() {
    idf_setup();

    while (1) {
        idf_loop();
    };
}
#endif
