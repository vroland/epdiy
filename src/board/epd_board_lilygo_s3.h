typedef struct {
  bool ep_latch_enable : 1;
  bool power_disable : 1;
  bool pos_power_enable : 1;
  bool neg_power_enable : 1;
  bool ep_scan_direction : 1;
  bool ep_stv : 1;
  bool ep_mode : 1;
  bool ep_output_enable : 1;
} epd_config_register_t;

static epd_config_register_t config_reg;