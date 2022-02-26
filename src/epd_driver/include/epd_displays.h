#pragma once
#include "epd_driver.h"

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief Default EPD displays struct(s)
 * error: initializer element is not constant if we set .waveform globally
 * Important: Definition should match table in epdiy Readme
 */
EpdDisplay ED047TC1;
EpdDisplay ED060SC4;
EpdDisplay ED097OC4;
EpdDisplay ED097TC2;
EpdDisplay ED097OC1;

void epdiy_display_default_configs();

#ifdef __cplusplus
}
#endif
