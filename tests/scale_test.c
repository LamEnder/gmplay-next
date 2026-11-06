// Host-side test for src/scale.c: compares the fused scaler against a
// reference bilinear scaler, checks nothing is drawn outside the video rect,
// and that the black/white path matches the screen dither LUT exactly.
// Run with tests/run-host-tests.sh
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "scale.h"
#include <libavutil/pixdesc.h>
Options opt;
void canvas_init(Canvas *c, const Screen *s, uint8_t *buf, bool rot) { c->px = buf; c->rot = rot; c->w = (int)(rot ? s->h : s->w); c->h = (int)(rot ? s->w : s->h); }
const AVPixFmtDescriptor *av_pix_fmt_desc_get(enum AVPixelFormat f) { (void)f; return NULL; }
// Reference: bilinear sample of src at canvas-rect pixel (vx,vy), same conventions.
static double ref(const uint8_t *src, int ls, int sw, int sh, int dw, int dh, int vx, int vy, int lim) {
    double sx = (vx + 0.5) * sw / dw - 0.5, sy = (vy + 0.5) * sh / dh - 0.5;
    if (sx < 0) sx = 0; if (sy < 0) sy = 0;
    if (sx > sw - 1) sx = sw - 1; if (sy > sh - 1) sy = sh - 1;
    int x0 = (int)sx, y0 = (int)sy, x1 = x0 + 1 < sw ? x0 + 1 : x0, y1 = y0 + 1 < sh ? y0 + 1 : y0;
    double fx = sx - x0, fy = sy - y0;
    double v = (src[y0*ls+x0]*(1-fx) + src[y0*ls+x1]*fx)*(1-fy) + (src[y1*ls+x0]*(1-fx) + src[y1*ls+x1]*fx)*fy;
    return lim ? (v - 16) * 255 / 219 : v;
}
int main(void) {
    int W = 1072, H = 1448, worst = 0, fail = 0;
    int cases[][2] = { {480, 360}, {1920, 1080}, {640, 480}, {1, 1}, {7, 3} };
    for (int ci = 0; ci < 5; ci++) for (int rot = 0; rot < 2; rot++) for (int lim = 0; lim < 2; lim++) {
        int sw = cases[ci][0], sh = cases[ci][1], ls = sw + 13;
        uint8_t *src = malloc((size_t)ls * sh);
        for (int y = 0; y < sh; y++) for (int x = 0; x < ls; x++) src[y*ls+x] = (uint8_t)(lim ? 16 + (x*7 + y*13 + (x*y)%31) % 220 : (x*7 + y*13 + (x*y)%31) % 256);
        Screen s = {0}; s.w = W; s.h = H; s.stride = W + 16; s.bpp = 8;
        s.fb = calloc((size_t)s.stride * H, 1);
        for (int b = 0; b < 64; b++) for (int v = 0; v < 256; v++) s.lut_video[b][v] = (uint8_t)v;   // no dither
        Canvas c; canvas_init(&c, &s, NULL, rot);
        Rect vr = { 37, 21, c.w - 80, c.h - 50 };
        Scaler sc = {0};
        opt.gray = true;   // identity LUT -> raw scaled values
        if (!scaler_setup(&sc, &s, &c, vr, sw, sh, ls, lim)) { puts("setup failed"); return 1; }
        scaler_draw(&sc, &s, src, ls);
        // black/white path must equal the screen's bw dither LUT applied to those values
        uint8_t *gray = malloc((size_t)s.stride * H); memcpy(gray, s.fb, (size_t)s.stride * H);
        Screen bw = s; bw.fb = calloc((size_t)s.stride * H, 1);
        for (int b = 0; b < 64; b++) for (int x = 0; x < 256; x++) bw.lut_video[b][x] = x > ((b*37)%64) * 4 + 2 ? 255 : 0;
        Scaler sb = {0}; opt.gray = false;
        if (!scaler_setup(&sb, &bw, &c, vr, sw, sh, ls, lim)) { puts("setup failed"); return 1; }
        scaler_draw(&sb, &bw, src, ls);
        int bwbad = 0;
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            size_t o = (size_t)y * s.stride + x;
            bool inside = x >= sb.fr.x && x < sb.fr.x + sb.fr.w && y >= sb.fr.y && y < sb.fr.y + sb.fr.h;
            uint8_t want = inside ? bw.lut_video[(y & 7) * 8 + (x & 7)][gray[o]] : 0;
            if (bw.fb[o] != want) bwbad++;
        }
        scaler_free(&sb); free(bw.fb); free(gray);
        int err = 0;
        for (int vy = 0; vy < vr.h; vy++) for (int vx = 0; vx < vr.w; vx++) {
            int lx = vr.x + vx, ly = vr.y + vy, fx = rot ? c.h - 1 - ly : lx, fy = rot ? lx : ly;
            double r = ref(src, ls, sw, sh, vr.w, vr.h, vx, vy, lim);
            r = r < 0 ? 0 : r > 255 ? 255 : r;
            int d = abs((int)s.fb[(size_t)fy * s.stride + fx] - (int)lround(r));
            if (d > err) err = d;
        }
        // nothing outside the fb rect may be touched
        int stray = 0;
        Rect f = sc.fr;
        for (int y = 0; y < H; y++) for (int x = 0; x < (int)s.stride; x++)
            if ((x < f.x || x >= f.x + f.w || y < f.y || y >= f.y + f.h) && s.fb[(size_t)y*s.stride+x]) stray++;
        printf("%4dx%-4d rot=%d limited=%d  max err %d  stray %d  bw mismatches %d\n", sw, sh, rot, lim, err, stray, bwbad);
        if (err > worst) worst = err;
        if (err > 3 || stray || bwbad) fail = 1;
        scaler_free(&sc); free(src); free(s.fb);
    }
    printf(fail ? "FAIL\n" : "PASS (worst error %d/255)\n", worst);
    return fail;
}
