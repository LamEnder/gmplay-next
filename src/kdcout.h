#pragma once
#include "screen.h"

// Optional video output through kdc (per-pixel pulse-drive controller,
// custom_kindle_display_controller_driver). The video frame goes into a kdc
// frame buffer as plain gray (kdc dithers/quantizes and drives only changed
// pixels); kdc never blocks, it drops frames the panel can't keep up with.
//
// Needs the pulse waveform installed (kdc-pulse.sh install ...) and a
// calibrated preset (-K FILE). Without HAVE_KDC every call fails cleanly.
typedef struct KdcOut KdcOut;

// NULL on failure (reason printed); the caller falls back to FBInk.
KdcOut *kdcout_open(const Screen *s, const char *preset, bool gray, bool verbose);
void kdcout_close(KdcOut *o, bool verbose);

// Frame buffer in fb orientation (width = s->w, *stride bytes per row).
// All buffers start white; only ever draw inside the same video rect.
uint8_t *kdcout_begin(KdcOut *o, int *stride);
void kdcout_publish(KdcOut *o, Rect fb_rect);
// kdc hasn't taken the last frame yet: skip preparing another one (saves the
// single CPU core for decoding and for kdc's planner).
bool kdcout_busy(KdcOut *o);
void kdcout_clean(KdcOut *o);   // full GC16 refresh of the current frame
