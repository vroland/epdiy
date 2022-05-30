#include "epd_board.h"

#include "epd_driver.h"
#include <stddef.h>

const EpdBoardDefinition *epd_board = NULL;

void epd_set_board(const EpdBoardDefinition *board_definition) {
  epd_board = board_definition;
}
