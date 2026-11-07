#pragma once
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct { int x, y, w, h; } Rect;

static inline bool rect_hit(Rect r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

enum rotate_mode { ROT_AUTO, ROT_OFF, ROT_ON };
enum { OD_OFF, OD_ON, OD_MAX };   // overdrive levels
#define OD_ON_FPS 10.0             // OD_ON = OD_MAX capped at this rate (unless -f is given)

typedef struct {
    enum rotate_mode rotate;
    bool gray;          // 16-level gray video (GL16) instead of black/white (A2)
    double clean_secs;  // flashing refresh interval during playback, 0 = off
    int overdrive;      // OD_OFF / OD_ON / OD_MAX, see canvas_begin()
    double max_fps;     // cap on frames shown per second, 0 = no cap
    bool mute;          // no audio
    bool slow_scale;    // force the swscale path (for comparison)
    double av_delay;    // extra output latency to compensate (Bluetooth), secs
    const char *kdc_preset;   // -K: play video through kdc with this preset (NULL = FBInk)
    bool verbose;
} Options;

extern Options opt;
extern volatile sig_atomic_t g_stop;   // SIGINT/SIGTERM

double now_sec(void);
