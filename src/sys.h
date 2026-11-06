#pragma once
#include "common.h"

// Pause whatever else draws to the screen (stock UI, window manager, KOReader)
// while we play, and resume it afterwards. Processes that were already
// stopped (e.g. awesome, stopped by KOReader) are left alone.
// GMPLAY_FREEZE overrides the process name list (space separated).
void sys_freeze_ui(void);
void sys_thaw_ui(void);   // async-signal-safe

// Keep the Kindle from going to the screensaver; restores the previous value.
void sys_prevent_screensaver(bool on);
