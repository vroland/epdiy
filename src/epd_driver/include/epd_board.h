/**
 * @file "epd_board.h"
 * @brief Board-definitions provided by epdiy.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  bool ep_latch_enable : 1;
  bool ep_output_enable : 1;
  bool ep_sth : 1;
  bool ep_mode : 1;
  bool ep_stv : 1;
} epd_ctrl_state_t;

typedef struct {
  void (*init)(uint32_t epd_row_width);
  void (*deinit)(void);
  void (*set_ctrl)(epd_ctrl_state_t *, const epd_ctrl_state_t * const);
  void (*poweron)(epd_ctrl_state_t *);
  void (*poweroff)(epd_ctrl_state_t *);

  void (*temperature_init)(void);
  float (*ambient_temperature)(void);
} EpdBoardDefinition;

extern const EpdBoardDefinition *epd_board;

// Built in board definitions
extern const EpdBoardDefinition epd_board_lilygo_t5_47;
extern const EpdBoardDefinition epd_board_lilygo_t5_47_touch;
extern const EpdBoardDefinition epd_board_v2_v3;
extern const EpdBoardDefinition epd_board_v4;
extern const EpdBoardDefinition epd_board_v5;
extern const EpdBoardDefinition epd_board_v6;
extern const EpdBoardDefinition epd_board_s3_prototype;

