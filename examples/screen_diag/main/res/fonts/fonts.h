#pragma once

#include <epd_driver.h>

#ifdef DEFINE_FONTS
#include "alexandria.h"
#include "amiri.h"
#else
extern const EpdFont Alexandria;
extern const EpdFont Amiri;
#endif
