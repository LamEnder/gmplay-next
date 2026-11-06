#pragma once
#include "common.h"

// Touchscreen taps, in framebuffer coordinates. The touch device is grabbed
// so taps don't also reach the (frozen) Kindle UI / KOReader underneath.
//
// If taps land in the wrong place, set GMPLAY_TOUCH to any of
// "swap", "mirror_x", "mirror_y" (comma separated) - check with `gmplay -T`.
int input_open(uint32_t screen_w, uint32_t screen_h);
void input_close(void);
void input_release(void);   // async-signal-safe ungrab, for crash handlers

// Wait up to timeout_ms (0 = just poll, -1 = forever) for input.
// Returns true and fills x,y if a tap (finger lifted) happened.
bool input_tap(int timeout_ms, int *x, int *y);
