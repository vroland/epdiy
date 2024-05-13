/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen */

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h> // memset
#include "dragon.h"
#include "epd_highlevel.h"
#include "epdiy.h"

EpdiyHighlevelState hl;
// The epdiy framebuffer that is sent to the render engine
uint8_t * framebuffer;

// MODE_PACKING_8PPB does nothing (Want to intent using 8BPP)
// MODE_DU only monochrome no grays (But still 4BPP)
// MODE_GC16 grays "slow mode" (4BPP)
int draw_mode = MODE_PACKING_8PPB;

// choose the default demo board depending on the architecture
#ifdef CONFIG_IDF_TARGET_ESP32
#define DEMO_BOARD epd_board_v6
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define DEMO_BOARD epd_board_v7
#endif

void idf_loop() {
    EpdRect dragon_area = 
    {.x = 0, .y = 0, 
    .width = dragon_width, 
    .height = dragon_height};

    int temperature = 25;

    epd_poweron();
    epd_fullclear(&hl, temperature);
    framebuffer = epd_hl_get_framebuffer(&hl);

    //epd_copy_to_framebuffer(dragon_area, dragon_data, epd_hl_get_framebuffer(&hl));
    epd_copy_to_framebuffer(dragon_area, dragon_data, framebuffer);
    // NOW Let's draw over and on top of Dragon image a full 10x black lines
    for (int y = 0; y < 10; y++) {
        switch (draw_mode)
        {
        case 1:
            /* MODE_DU 4PPB */
            memset(&framebuffer[y*epd_width()/2], 0x00, epd_width()/2);
            break;
        
        default:
            /* MODE_DU 8PPB */
            memset(&framebuffer[y*epd_width()/8], 0x00, epd_width()/8);
            break;
        }
    }
    /**
     * @brief EXPECTED: Setting full 3 lines to 0x00 it should draw it on display.
     *        RESULT  : Draws nothing in MODE_PACKING_8PPB
     *        RESULT  : Draws OK 10 black lines in MODE_DU & MODE_GC16
     */

    enum EpdDrawError _err = epd_hl_update_screen(&hl, draw_mode, temperature);
    epd_poweroff();

    vTaskDelay(50000);
}

void idf_setup() {
    epd_init(&DEMO_BOARD, &ED060XC3, EPD_LUT_1K);
    epd_set_vcom(1560);
    hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
}

#ifndef ARDUINO_ARCH_ESP32
void app_main() {
    idf_setup();

    while (1) {
        idf_loop();
    };
}
#endif
