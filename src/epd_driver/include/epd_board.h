/**
 * @file "epd_board.h"
 * @brief Board-definitions provided by epdiy.
 */

#pragma once

#include <stdint.h>

typedef struct {
  void (*init)(uint32_t epd_row_width);
  void (*deinit)(void);
  void (*poweron)(void);
  void (*poweroff)(void);
  void (*start_frame)(void);
  void (*latch_row)(void);
  void (*end_frame)(void);

  void (*temperature_init)(void);
  float (*ambient_temperature)(void);
} EpdBoardDefinition;

extern const EpdBoardDefinition *epd_board;

// Built in board definitions
extern const EpdBoardDefinition epd_board_lilygo_t5_47;
extern const EpdBoardDefinition epd_board_v2_v3;
extern const EpdBoardDefinition epd_board_v4;
extern const EpdBoardDefinition epd_board_v5;
extern const EpdBoardDefinition epd_board_v6;
