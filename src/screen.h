#pragma once
#include "common.h"
#include "fbink.h"

// Framebuffer + e-ink refresh (via FBInk), plus a logical gray8 "canvas" that
// is either the screen's own orientation or rotated 90° clockwise onto it.
typedef struct {
    int fd;
    FBInkConfig cfg;
    uint8_t *fb;
    uint32_t w, h, stride, bpp;
    uint8_t *saved;                // screen contents from before we started
    bool no_submission;            // kernel lacks WAIT_FOR_UPDATE_SUBMISSION
    uint8_t lut_video[64][256];    // ordered dither for video ([bayer cell][gray])
    uint8_t lut_gray[64][256];     // 16-level ordered dither for UI / paused frames
} Screen;

typedef struct {
    uint8_t *px;   // gray8, stride == w
    int w, h;      // logical size
    bool rot;      // logical (x,y) shows at fb (W-1-y, x)
} Canvas;

enum present_mode {
    PRESENT_VIDEO,   // video LUT, A2 (or GL16 with -g); with overdrive only waits
                     // until the previous update has been latched, not finished
    PRESENT_UI,      // gray LUT, GL16
    PRESENT_FLASH,   // gray LUT, GC16 flashing - clears ghosting
};

int screen_open(Screen *s);
void screen_close(Screen *s);
void screen_save(Screen *s);
void screen_restore(Screen *s);
void screen_wait(Screen *s);

void canvas_init(Canvas *c, const Screen *s, uint8_t *buf, bool rot);
void canvas_present(Screen *s, const Canvas *c, Rect r, enum present_mode mode);
// canvas_present in parts, for callers that draw into the fb themselves:
void canvas_begin(Screen *s, enum present_mode mode);                     // wait for the panel if needed
void canvas_refresh(Screen *s, const Canvas *c, Rect r, enum present_mode mode);
void canvas_from_fb(const Canvas *c, int fx, int fy, int *lx, int *ly);
