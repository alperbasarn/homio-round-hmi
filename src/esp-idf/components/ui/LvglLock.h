#pragma once
// The LVGL lock as a caller precondition (SCRN-03). Views never take the lock: the display task
// holds it around the frame, and a recursive take inside a view would only hide a caller that
// forgot. A debug device build asserts that the calling task holds it at every view entry point;
// release builds (NDEBUG) and the host build, which has no LVGL, compile the check out.
#if defined(ESP_PLATFORM) && !defined(NDEBUG)
#include <cassert>
#include "LvglDisplay.h"
#define QNOB_LVGL_ASSERT_LOCKED() assert(LvglDisplay::lockedByCurrentTask())
#else
#define QNOB_LVGL_ASSERT_LOCKED() ((void)0)
#endif
