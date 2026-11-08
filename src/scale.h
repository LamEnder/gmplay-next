#pragma once
#include "screen.h"

// Fast video path: bilinear-scale an 8-bit luma plane, expand limited range,
// dither and rotate it straight into an 8bpp framebuffer.
// (Luma is the gray image, so chroma is never touched.)
//
// Separable, in two passes per framebuffer row:
//  1. each source line (row, or column when rotated) that's needed is scaled
//     along the fb row direction once and cached;
//  2. the fb row is a fixed-weight blend of two cached lines + an ordered
//     dither threshold compare - straight-line code GCC vectorizes to NEON.
typedef struct {
    int sw, sh, ls;          // source size + linesize the tables were built for
    bool limited, rot, gray;
    bool raw;                // output plain gray, no dither (set before scaler_draw; kdc)
    Rect vr;                 // destination rect on the canvas
    Rect fr;                 // destination rect in fb coordinates
    int nsrc;                // source lines along the outer axis
    int16_t *oi;             // per fb row: source line index
    uint8_t *ow;             //   ... and weight of line index+1 (0..255)
    int32_t *ioff0, *ioff1;  // per fb column: source sample offsets within a line
    uint8_t *iw;             //   ... and weight of the second sample
    uint8_t *cache[2];       // two scaled source lines
    int tag[2];              // which source line each cache slot holds (-1 = none)
    uint8_t *thr[8];         // black/white dither threshold per fb column, per fb row % 8
    uint8_t range[256];      // limited (16..235) -> full range
} Scaler;

// Can the fast path take this frame's pixel format (AV_PIX_FMT_*)?
bool scaler_supports(int pix_fmt);

// (Re)build tables if the geometry changed. Returns false on OOM.
bool scaler_setup(Scaler *sc, const Screen *s, const Canvas *c, Rect vr, int sw, int sh, int ls, bool limited);
void scaler_free(Scaler *sc);

// Draw into the framebuffer (caller waits for/refreshes the screen).
void scaler_draw(Scaler *sc, Screen *s, const uint8_t *src, int ls);
