#include "screen.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t bayer8[64] = {
     0, 32,  8, 40,  2, 34, 10, 42, 48, 16, 56, 24, 50, 18, 58, 26,
    12, 44,  4, 36, 14, 46,  6, 38, 60, 28, 52, 20, 62, 30, 54, 22,
     3, 35, 11, 43,  1, 33,  9, 41, 51, 19, 59, 27, 49, 17, 57, 25,
    15, 47,  7, 39, 13, 45,  5, 37, 63, 31, 55, 23, 61, 29, 53, 21,
};

static void build_luts(Screen *s) {
    for (int b = 0; b < 64; b++) {
        for (int v = 0; v < 256; v++) {
            int level = (v * 15 + bayer8[b] * 255 / 64) / 255;   // 16 levels = panel's 4-bit gray
            s->lut_gray[b][v] = (uint8_t)(level > 15 ? 255 : level * 17);
            s->lut_video[b][v] = opt.gray ? s->lut_gray[b][v] : (v > bayer8[b] * 4 + 2 ? 0xFF : 0x00);
        }
    }
}

int screen_open(Screen *s) {
    memset(s, 0, sizeof *s);
    s->fd = fbink_open();
    if (s->fd < 0) { fprintf(stderr, "gmplay: can't open framebuffer\n"); return -1; }
    s->cfg.is_quiet = !opt.verbose;
    if (fbink_init(s->fd, &s->cfg) < 0) { fprintf(stderr, "gmplay: FBInk init failed\n"); return -1; }
    FBInkState st;
    fbink_get_state(&s->cfg, &st);
    size_t len;
    s->fb = fbink_get_fb_pointer(s->fd, &len);
    s->w = st.screen_width;
    s->h = st.screen_height;
    s->stride = st.scanline_stride;
    s->bpp = st.bpp;
    if (!s->fb || (s->bpp != 8 && s->bpp != 32)) {
        fprintf(stderr, "gmplay: unsupported framebuffer (%u bpp)\n", s->bpp);
        return -1;
    }
    build_luts(s);
    if (opt.verbose)
        fprintf(stderr, "gmplay: screen %ux%u, %u bpp, stride %u, device %s\n",
                s->w, s->h, s->bpp, s->stride, st.device_name);
    return 0;
}

void screen_close(Screen *s) {
    free(s->saved);
    s->saved = NULL;
    if (s->fd >= 0) fbink_close(s->fd);
}

void screen_save(Screen *s) {
    size_t n = (size_t)s->stride * s->h;
    s->saved = malloc(n);
    if (s->saved) memcpy(s->saved, s->fb, n);
}

static void refresh(Screen *s, Rect fr, int wfm, bool flash) {
    FBInkConfig cfg = s->cfg;
    cfg.wfm_mode = (uint8_t)wfm;
    cfg.is_flashing = flash;
    fbink_refresh(s->fd, (uint32_t)fr.y, (uint32_t)fr.x, (uint32_t)fr.w, (uint32_t)fr.h, &cfg);
}

void screen_restore(Screen *s) {
    screen_wait(s);
    if (s->saved) memcpy(s->fb, s->saved, (size_t)s->stride * s->h);
    else memset(s->fb, 0xFF, (size_t)s->stride * s->h);
    refresh(s, (Rect){ 0, 0, (int)s->w, (int)s->h }, WFM_GC16, true);
    screen_wait(s);
}

void screen_wait(Screen *s) {
    fbink_wait_for_complete(s->fd, LAST_MARKER);
}

void canvas_init(Canvas *c, const Screen *s, uint8_t *buf, bool rot) {
    c->px = buf;
    c->rot = rot;
    c->w = (int)(rot ? s->h : s->w);
    c->h = (int)(rot ? s->w : s->h);
}

void canvas_from_fb(const Canvas *c, int fx, int fy, int *lx, int *ly) {
    if (c->rot) { *lx = fy; *ly = c->h - 1 - fx; }
    else { *lx = fx; *ly = fy; }
}

// Logical rect -> fb rect.
static Rect to_fb(const Canvas *c, Rect r) {
    return c->rot ? (Rect){ c->h - (r.y + r.h), r.x, r.h, r.w } : r;
}

static Rect clip(const Canvas *c, Rect r) {
    if (r.x < 0) { r.w += r.x; r.x = 0; }
    if (r.y < 0) { r.h += r.y; r.y = 0; }
    if (r.x + r.w > c->w) r.w = c->w - r.x;
    if (r.y + r.h > c->h) r.h = c->h - r.y;
    if (r.w < 0) r.w = 0;
    if (r.h < 0) r.h = 0;
    return r;
}

static inline void put32(Screen *s, int x, int y, uint8_t v) {
    uint8_t *p = s->fb + (size_t)y * s->stride + (size_t)x * 4;
    p[0] = p[1] = p[2] = v;
    p[3] = 0xFF;
}

// Convert + dither a logical rect into the framebuffer.
static void blit(Screen *s, const Canvas *c, Rect r, const uint8_t (*lut)[256]) {
    if (!c->rot) {
        for (int y = r.y; y < r.y + r.h; y++) {
            const uint8_t *src = c->px + (size_t)y * c->w;
            const uint8_t *row = lut[(y & 7) * 8];
            if (s->bpp == 8) {
                uint8_t *dst = s->fb + (size_t)y * s->stride;
                for (int x = r.x; x < r.x + r.w; x++) dst[x] = row[(x & 7) * 256 + src[x]];
            } else {
                for (int x = r.x; x < r.x + r.w; x++) put32(s, x, y, row[(x & 7) * 256 + src[x]]);
            }
        }
        return;
    }
    // Rotated: fb (fx,fy) shows logical (fy, H-1-fx). Walk fb in bands of 32
    // rows so reads stay sequential along a logical row and writes stay cached.
    Rect f = to_fb(c, r);
    for (int fy0 = f.y; fy0 < f.y + f.h; fy0 += 32) {
        int fy1 = fy0 + 32 < f.y + f.h ? fy0 + 32 : f.y + f.h;
        for (int fx = f.x; fx < f.x + f.w; fx++) {
            const uint8_t *src = c->px + (size_t)(c->h - 1 - fx) * c->w;
            int bx = (fx & 7) * 256;
            if (s->bpp == 8) {
                uint8_t *dst = s->fb + fx;
                for (int fy = fy0; fy < fy1; fy++)
                    dst[(size_t)fy * s->stride] = lut[(fy & 7) * 8][bx + src[fy]];
            } else {
                for (int fy = fy0; fy < fy1; fy++) put32(s, fx, fy, lut[(fy & 7) * 8][bx + src[fy]]);
            }
        }
    }
}

void canvas_begin(Screen *s, enum present_mode mode) {
    // Overdrive: the panel may still be animating the previous frame, but once
    // the controller has latched (submitted) it, the fb is safe to overwrite.
    // Writing before that point is what causes torn/glitched frames.
    // Overlapping updates still glitch where an A2 transition gets a new target
    // mid-way; OD_ON limits how often that happens with a frame-rate cap (see
    // player.c). Waiting for the earlier update to *finish* instead is no use:
    // the controller then serializes the collision and redoes the area, which
    // is slower than not overlapping at all (measured on a PW4).
    if (mode == PRESENT_VIDEO && opt.overdrive != OD_OFF && !s->no_submission) {
        if (fbink_wait_for_submission(s->fd, LAST_MARKER) != -ENOSYS) return;
        s->no_submission = true;   // unsupported kernel: fall back to waiting for completion
        if (opt.verbose) fprintf(stderr, "gmplay: no update-submission wait, overdrive disabled\n");
    }
    screen_wait(s);   // don't draw into a refresh in flight
}

void canvas_refresh(Screen *s, const Canvas *c, Rect r, enum present_mode mode) {
    r = clip(c, r);
    if (r.w <= 0 || r.h <= 0) return;
    int wfm = mode == PRESENT_FLASH ? WFM_GC16 : mode == PRESENT_UI ? WFM_GL16 : (opt.gray ? WFM_GL16 : WFM_A2);
    refresh(s, to_fb(c, r), wfm, mode == PRESENT_FLASH);
}

void canvas_present(Screen *s, const Canvas *c, Rect r, enum present_mode mode) {
    r = clip(c, r);
    if (r.w <= 0 || r.h <= 0) return;
    canvas_begin(s, mode);
    blit(s, c, r, mode == PRESENT_VIDEO ? (const uint8_t (*)[256])s->lut_video : (const uint8_t (*)[256])s->lut_gray);
    canvas_refresh(s, c, r, mode);
}

Rect canvas_rect_to_fb(const Canvas *c, Rect r) {
    return to_fb(c, clip(c, r));
}

void canvas_blit_raw(const Screen *s, const Canvas *c, Rect r, uint8_t *dst, int dst_stride) {
    static uint8_t ident[64][256];
    if (!ident[0][255])
        for (int b = 0; b < 64; b++)
            for (int v = 0; v < 256; v++) ident[b][v] = (uint8_t)v;
    Screen t = *s;
    t.fb = dst;
    t.stride = (uint32_t)dst_stride;
    t.bpp = 8;
    r = clip(c, r);
    if (r.w > 0 && r.h > 0) blit(&t, c, r, (const uint8_t (*)[256])ident);
}
