#include "ed097oc4.h"
#include <stdint.h>

typedef struct {
  bool ep_latch_enable : 1;
  bool power_disable : 1;
  bool pos_power_enable : 1;
  bool neg_power_enable : 1;
  bool ep_stv : 1;
  bool ep_scan_direction : 1;
  bool ep_mode : 1;
  bool ep_output_enable : 1;
} epd_config_register_t;

static void config_reg_init(epd_config_register_t *cfg) {
  cfg->ep_latch_enable = false;
  cfg->power_disable = true;
  cfg->pos_power_enable = false;
  cfg->neg_power_enable = false;
  cfg->ep_stv = true;
  cfg->ep_scan_direction = true;
  cfg->ep_mode = false;
  cfg->ep_output_enable = false;
}


inline static void IRAM_ATTR push_cfg_bit(bool bit);
void IRAM_ATTR busy_delay(uint32_t cycles);
inline static void fast_gpio_set_hi(gpio_num_t gpio_num);
inline static void fast_gpio_set_lo(gpio_num_t gpio_num);

static void IRAM_ATTR push_cfg(const epd_config_register_t *cfg) {
  fast_gpio_set_lo(CFG_STR);

  // push config bits in reverse order
  push_cfg_bit(cfg->ep_output_enable);
  push_cfg_bit(cfg->ep_mode);
  push_cfg_bit(cfg->ep_scan_direction);
  push_cfg_bit(cfg->ep_stv);

  push_cfg_bit(cfg->neg_power_enable);
  push_cfg_bit(cfg->pos_power_enable);
  push_cfg_bit(cfg->power_disable);
  push_cfg_bit(cfg->ep_latch_enable);

  fast_gpio_set_hi(CFG_STR);
}


static void cfg_poweron(epd_config_register_t *cfg) {
  // POWERON
  cfg->power_disable = false;
  push_cfg(cfg);
  busy_delay(100 * 240);
  cfg->neg_power_enable = true;
  push_cfg(cfg);
  busy_delay(500 * 240);
  cfg->pos_power_enable = true;
  push_cfg(cfg);
  busy_delay(100 * 240);
  cfg->ep_stv = true;
  push_cfg(cfg);
  fast_gpio_set_hi(STH);
  // END POWERON
}


static void cfg_poweroff(epd_config_register_t *cfg) {
  // POWEROFF
  cfg->pos_power_enable = false;
  push_cfg(cfg);
  busy_delay(10 * 240);
  cfg->neg_power_enable = false;
  push_cfg(cfg);
  busy_delay(100 * 240);
  cfg->power_disable = true;
  push_cfg(cfg);
  // END POWEROFF
}
