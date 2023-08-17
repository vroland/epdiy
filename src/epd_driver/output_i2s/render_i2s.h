#include "epd_driver.h"

#include <driver/gpio.h>
#include <esp_system.h>  // for ESP_IDF_VERSION_VAL
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include <hal/gpio_ll.h>
#include <soc/gpio_struct.h>
#endif

#include "sdkconfig.h"
#include "../output_common/render_context.h"


void i2s_feed_frame(RenderContext_t *ctx, int thread_id);
void i2s_output_frame(RenderContext_t *ctx, int thread_id);
void i2s_do_update(RenderContext_t *ctx);
void i2s_deinit();

void epd_push_pixels_i2s(RenderContext_t *ctx, EpdRect area, short time, int color);


#ifdef CONFIG_IDF_TARGET_ESP32

/*
 * Write bits directly using the registers in the ESP32.
 * Won't work for some pins (>= 32).
 */
inline void fast_gpio_set_hi(gpio_num_t gpio_num) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    gpio_dev_t *device = GPIO_LL_GET_HW(GPIO_PORT_0);
    device->out_w1ts   = (1 << gpio_num);
#else
    GPIO.out_w1ts = (1 << gpio_num);
#endif
}

inline void fast_gpio_set_lo(gpio_num_t gpio_num) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    gpio_dev_t *device = GPIO_LL_GET_HW(GPIO_PORT_0);
    device->out_w1tc   = (1 << gpio_num);
#else
    GPIO.out_w1tc = (1 << gpio_num);
#endif
}

#endif
