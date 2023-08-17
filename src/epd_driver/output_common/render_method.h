#include "sdkconfig.h"

#ifdef CONFIG_IDF_TARGET_ESP32
#define RENDER_METHOD_I2S 1
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define RENDER_METHOD_LCD 1
#else
#error "unknown chip, cannot choose render method!"
#endif
