#include "scale.h"

#include <stdlib.h>
#include <string.h>

#include <libavutil/pixdesc.h>

bool scaler_supports(int pix_fmt) {
    const AVPixFmtDescriptor *d = av_pix_fmt_desc_get(pix_fmt);
    if (!d || (d->flags & (AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_BITSTREAM |
                           AV_PIX_FMT_FLAG_HWACCEL | AV_PIX_FMT_FLAG_BE)))
        return false;
    const AVComponentDescriptor *y = &d->comp[0];
    return y->depth == 8 && y->plane == 0 && y->step == 1 && y->offset == 0 && y->shift == 0;
}

// Bilinear source position for each of dst outputs along one axis
// (pixel-center aligned). idx+1 is always a valid sample.
static void axis(int src, int dst, int16_t *idx, uint8_t *w) {
    for (int o = 0; o < dst; o++) {
        double s = (o + 0.5) * src / dst - 0.5;
        if (s < 0) s = 0;
        int i = (int)s;
        double f = s - i;
        if (i >= src - 1) { i = src > 1 ? src - 2 : 0; f = src > 1 ? 1.0 : 0.0; }
        idx[o] = (int16_t)i;
        w[o] = (uint8_t)(f * 255 + 0.5);
    }
}

void scaler_free(Scaler *sc) {
    free(sc->oi); free(sc->ow); free(sc->ioff0); free(sc->ioff1); free(sc->iw);
    free(sc->cache[0]); free(sc->cache[1]);
    for (int i = 0; i < 8; i++) free(sc->thr[i]);
    memset(sc, 0, sizeof *sc);
}

bool scaler_setup(Scaler *sc, const Screen *s, const Canvas *c, Rect vr, int sw, int sh, int ls, bool limited) {
    if (sc->oi && sc->sw == sw && sc->sh == sh && sc->ls == ls && sc->limited == limited &&
        sc->rot == c->rot && sc->gray == opt.gray && !memcmp(&sc->vr, &vr, sizeof vr))
        return true;
    scaler_free(sc);
    sc->sw = sw; sc->sh = sh; sc->ls = ls; sc->limited = limited; sc->vr = vr;
    sc->rot = c->rot; sc->gray = opt.gray;
    // fb rect of vr (see canvas rotation in screen.c)
    Rect f = sc->fr = c->rot ? (Rect){ c->h - (vr.y + vr.h), vr.x, vr.h, vr.w } : vr;
    sc->nsrc = c->rot ? sw : sh;
    sc->tag[0] = sc->tag[1] = -1;

    int16_t *xi = malloc(sizeof *xi * (size_t)vr.w), *yi = malloc(sizeof *yi * (size_t)vr.h);
    uint8_t *xw = malloc((size_t)vr.w), *yw = malloc((size_t)vr.h);
    sc->oi = malloc(sizeof *sc->oi * (size_t)f.h);
    sc->ow = malloc((size_t)f.h);
    sc->ioff0 = malloc(sizeof *sc->ioff0 * (size_t)f.w);
    sc->ioff1 = malloc(sizeof *sc->ioff1 * (size_t)f.w);
    sc->iw = malloc((size_t)f.w);
    sc->cache[0] = malloc((size_t)f.w);
    sc->cache[1] = malloc((size_t)f.w);
    bool ok = xi && yi && xw && yw && sc->oi && sc->ow && sc->ioff0 && sc->ioff1 && sc->iw &&
              sc->cache[0] && sc->cache[1];
    for (int r = 0; r < 8 && ok; r++) ok = (sc->thr[r] = malloc((size_t)f.w)) != NULL;
    if (ok) {
        axis(sw, vr.w, xi, xw);
        axis(sh, vr.h, yi, yw);
        if (c->rot) {
            // fb row = canvas x (source column); fb column runs canvas y bottom-up (source rows)
            memcpy(sc->oi, xi, sizeof *xi * (size_t)vr.w);
            memcpy(sc->ow, xw, (size_t)vr.w);
            for (int k = 0; k < vr.h; k++) {
                int j = yi[vr.h - 1 - k];
                sc->ioff0[k] = j * ls;
                sc->ioff1[k] = (j + 1 < sh ? j + 1 : j) * ls;
                sc->iw[k] = yw[vr.h - 1 - k];
            }
        } else {
            memcpy(sc->oi, yi, sizeof *yi * (size_t)vr.h);
            memcpy(sc->ow, yw, (size_t)vr.h);
            for (int k = 0; k < vr.w; k++) {
                sc->ioff0[k] = xi[k];
                sc->ioff1[k] = xi[k] + 1 < sw ? xi[k] + 1 : xi[k];
                sc->iw[k] = xw[k];
            }
        }
        for (int v = 0; v < 256; v++) {
            int r = limited ? ((v - 16) * 255 + 109) / 219 : v;
            sc->range[v] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
        }
        // Black/white: output is 255 where value > threshold. Derive the
        // thresholds from the screen's own dither LUT so both paths match.
        for (int r = 0; r < 8; r++) {
            for (int k = 0; k < f.w; k++) {
                const uint8_t *l = s->lut_video[r * 8 + ((f.x + k) & 7)];
                int t = 0;
                while (t < 256 && l[t] == 0) t++;
                sc->thr[r][k] = (uint8_t)(t - 1 < 0 ? 0 : t - 1);   // t-1 = last value that's black
            }
        }
    }
    free(xi); free(yi); free(xw); free(yw);
    if (!ok) scaler_free(sc);
    return ok;
}

// Pass 1: scale source line `n` along the fb row direction into `out`.
static void scale_line(const Scaler *sc, const uint8_t *src, int n, uint8_t *restrict out) {
    const int32_t *o0 = sc->ioff0, *o1 = sc->ioff1;
    const uint8_t *w = sc->iw, *range = sc->range;
    const uint8_t *p = sc->rot ? src + n : src + (size_t)n * sc->ls;   // column n / row n
    for (int k = 0; k < sc->fr.w; k++)
        out[k] = range[(p[o0[k]] * (256 - w[k]) + p[o1[k]] * w[k] + 128) >> 8];
}

static const uint8_t *cached_line(Scaler *sc, const uint8_t *src, int n, int keep) {
    if (sc->tag[0] == n) return sc->cache[0];
    if (sc->tag[1] == n) return sc->cache[1];
    int slot = sc->tag[0] == keep ? 1 : 0;   // don't evict the other line this row needs
    scale_line(sc, src, n, sc->cache[slot]);
    sc->tag[slot] = n;
    return sc->cache[slot];
}

void scaler_draw(Scaler *sc, Screen *s, const uint8_t *src, int ls) {
    (void)ls;   // baked into the tables by scaler_setup
    const Rect f = sc->fr;
    sc->tag[0] = sc->tag[1] = -1;   // new frame: cached lines are stale
    for (int k = 0; k < f.h; k++) {
        int i = sc->oi[k], i1 = i + 1 < sc->nsrc ? i + 1 : i;
        const uint8_t *restrict a = cached_line(sc, src, i, i1);
        const uint8_t *restrict b = cached_line(sc, src, i1, i);
        unsigned w = sc->ow[k], w0 = 256 - w;
        int fy = f.y + k;
        uint8_t *restrict dst = s->fb + (size_t)fy * s->stride + f.x;
        // Pass 2: blend + dither. Straight-line so it vectorizes.
        if (!sc->gray) {
            const uint8_t *restrict t = sc->thr[fy & 7];
            for (int x = 0; x < f.w; x++) {
                unsigned v = (a[x] * w0 + b[x] * w + 128) >> 8;
                dst[x] = v > t[x] ? 0xFF : 0x00;
            }
        } else {
            const uint8_t (*lut)[256] = (const uint8_t (*)[256])s->lut_video[(fy & 7) * 8];
            for (int x = 0; x < f.w; x++) {
                unsigned v = (a[x] * w0 + b[x] * w + 128) >> 8;
                dst[x] = lut[(f.x + x) & 7][v];
            }
        }
    }
}
