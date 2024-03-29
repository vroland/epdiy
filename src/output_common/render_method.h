#include "sdkconfig.h"

#ifdef CONFIG_IDF_TARGET_ESP32
#define RENDER_METHOD_I2S 1
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define RENDER_METHOD_LCD 1
#else
#error "unknown chip, cannot choose render method!"
#endif

#ifdef __clang__
#define IRAM_ATTR 
// define this if we're using clangd to make it accept the GCC builtin
void __assert_func (const char* file, int line, const char* func,
               const char* failedexpr);
#endif