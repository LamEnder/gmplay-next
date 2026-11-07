#include "kdcout.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_KDC
#include "kdc.h"

struct KdcOut {
    kdc_t *k;
};

KdcOut *kdcout_open(const Screen *s, const char *preset, bool gray, bool verbose) {
    kdc_config_t c;
    kdc_config_default(&c);
    char err[200];
    if (preset && kdc_config_load_doses(&c, preset, err, sizeof err) < 0) {
        fprintf(stderr, "gmplay: kdc preset: %s\n", err);
        return NULL;
    }
    c.mode = gray ? KDC_MODE_GRAY4 : KDC_MODE_DITHER;
    c.gray_dither = 1;          // video: dither between the 4 shades
    c.verbose = verbose;
    kdc_t *k = NULL;
    int r = kdc_open(&k, &c);
    if (r != KDC_OK) {
        fprintf(stderr, "gmplay: kdc unavailable (%d): %s; using FBInk\n", r, k ? kdc_last_error(k) : "");
        kdc_close(k);
        return NULL;
    }
    if (kdc_width(k) != (int)s->w || kdc_height(k) != (int)s->h) {
        fprintf(stderr, "gmplay: kdc screen %dx%d != fb %ux%u; using FBInk\n",
                kdc_width(k), kdc_height(k), s->w, s->h);
        kdc_close(k);
        return NULL;
    }
    KdcOut *o = malloc(sizeof *o);
    if (!o) { kdc_close(k); return NULL; }
    o->k = k;
    return o;
}

void kdcout_close(KdcOut *o, bool verbose) {
    if (!o) return;
    kdc_flush(o->k, 3000);
    if (verbose) {
        kdc_stats_t st;
        kdc_get_stats(o->k, &st);
        fprintf(stderr, "gmplay: kdc: %llu scans, %llu updates (%llu long), frames in %llu, dropped %llu, "
                        "scan %.1f ms, plan %.2f ms, %d C, errors %llu\n",
                (unsigned long long)st.scans, (unsigned long long)st.updates,
                (unsigned long long)st.long_updates, (unsigned long long)st.frames_in,
                (unsigned long long)st.frames_dropped, st.avg_scan_ms, st.avg_plan_ms,
                st.temperature_c, (unsigned long long)st.submit_errors);
        if (*kdc_last_error(o->k)) fprintf(stderr, "gmplay: kdc: %s\n", kdc_last_error(o->k));
    }
    kdc_close(o->k);   // closing refresh leaves the EPDC consistent for FBInk
    free(o);
}

uint8_t *kdcout_begin(KdcOut *o, int *stride) {
    *stride = kdc_width(o->k);
    return kdc_frame_begin(o->k, 0);
}

void kdcout_publish(KdcOut *o, Rect r) {
    kdc_rect_t d = { r.x, r.y, r.w, r.h };
    kdc_frame_publish(o->k, &d, 1);
}

void kdcout_clean(KdcOut *o) { kdc_clean(o->k); }

bool kdcout_busy(KdcOut *o) { return kdc_frame_pending(o->k) != 0; }

#else   // built without kdc

KdcOut *kdcout_open(const Screen *s, const char *preset, bool gray, bool verbose) {
    (void)s; (void)preset; (void)gray; (void)verbose;
    fprintf(stderr, "gmplay: built without kdc (meson -Dkdc_dir=...); using FBInk\n");
    return NULL;
}
void kdcout_close(KdcOut *o, bool verbose) { (void)o; (void)verbose; }
uint8_t *kdcout_begin(KdcOut *o, int *stride) { (void)o; *stride = 0; return NULL; }
void kdcout_publish(KdcOut *o, Rect r) { (void)o; (void)r; }
void kdcout_clean(KdcOut *o) { (void)o; }
bool kdcout_busy(KdcOut *o) { (void)o; return false; }

#endif
