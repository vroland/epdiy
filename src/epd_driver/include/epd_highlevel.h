/**
 * High-level API for epdiy.
 */

#include "epd_driver.h"

/// Holds the internal state of the high-level API.
typedef struct {
  /// The "front" framebuffer object.
  uint8_t* front_fb;
  /// The "back" framebuffer object.
  uint8_t* back_fb;
  /// Buffer for holding the interlaced difference image.
  uint8_t* difference_fb;
  /// Tainted lines based on the last difference calculation.
  bool* dirty_lines;
  /// The waveform information to use.
  const EpdWaveform* waveform;
} EpdiyHighlevelState;


EpdiyHighlevelState epd_hl_init(const EpdWaveform* waveform);

/// Get the framebuffer to write to.
uint8_t* epd_hl_get_framebuffer(EpdiyHighlevelState* state);

enum EpdDrawError epd_hl_update_screen(EpdiyHighlevelState* state, enum EpdDrawMode mode, int temperature);

enum EpdDrawError epd_hl_update_area(EpdiyHighlevelState* state, enum EpdDrawMode mode, int temperature, EpdRect area);

void epd_hl_set_all_white(EpdiyHighlevelState* state);
