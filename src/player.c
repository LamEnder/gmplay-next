// Video playback with a tap-to-pause control overlay.
#include "player.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include "audio.h"
#include "input.h"
#include "kdcout.h"
#include "scale.h"
#include "ui.h"

typedef struct {
    AVFormatContext *fmt;
    AVCodecContext *dec;
    AVStream *st;
    int vidx;
    AVPacket *pkt;
    AVFrame *frm, *last;   // last = frame currently on screen
    struct SwsContext *sws;
    double tb, dur, length;
    int64_t start;
    bool flushed;
    double pos;            // pts of the frame on screen (secs)
    double next_pts;       // fallback pts for frames without timestamps
    Canvas cv;
    Rect vr;               // video rect on the canvas
    Audio *au;             // NULL = no sound
    Scaler sc;             // fast luma->fb path
    // stats (seconds)
    double t_dec, t_scale, t_show;
    unsigned decoded, shown, dropped;
} Video;

static const char *base_name(const char *p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static void fmt_time(char *buf, size_t n, double t) {
    if (t < 0) t = 0;
    int s = (int)t, h = s / 3600, m = s / 60 % 60;
    if (h) snprintf(buf, n, "%d:%02d:%02d", h, m, s % 60);
    else snprintf(buf, n, "%d:%02d", m, s % 60);
}

// Get the next decoded video frame into v->frm. 1 = frame, 0 = end, <0 = error.
static int next_frame(Video *v) {
    for (;;) {
        int e = avcodec_receive_frame(v->dec, v->frm);
        if (e >= 0) {
            v->decoded++;
            return 1;
        }
        if (e == AVERROR_EOF) return 0;
        if (e != AVERROR(EAGAIN)) return e;
        if (v->flushed) return 0;
        e = av_read_frame(v->fmt, v->pkt);
        if (e < 0) {   // EOF or read error: drain the decoder
            v->flushed = true;
            avcodec_send_packet(v->dec, NULL);
            continue;
        }
        if (v->pkt->stream_index == v->vidx) avcodec_send_packet(v->dec, v->pkt);
        av_packet_unref(v->pkt);
    }
}

static double frame_pts(Video *v, const AVFrame *f) {
    double pts = f->best_effort_timestamp != AV_NOPTS_VALUE
                     ? (double)(f->best_effort_timestamp - v->start) * v->tb
                     : v->next_pts;
    v->next_pts = pts + v->dur;
    return pts;
}

// Scale a frame into the video rect of the canvas.
static void draw_frame(Video *v, const AVFrame *f) {
    v->sws = sws_getCachedContext(v->sws, f->width, f->height, f->format, v->vr.w, v->vr.h,
                                  AV_PIX_FMT_GRAY8, SWS_BILINEAR, NULL, NULL, NULL);
    if (!v->sws) return;
    uint8_t *dst[4] = { v->cv.px + (size_t)v->vr.y * v->cv.w + v->vr.x, NULL, NULL, NULL };
    int ls[4] = { v->cv.w, 0, 0, 0 };
    sws_scale(v->sws, (const uint8_t *const *)f->data, f->linesize, 0, f->height, dst, ls);
}

static void keep_last(Video *v) {
    av_frame_unref(v->last);
    av_frame_move_ref(v->last, v->frm);
}

static void seek_to(Video *v, double target) {
    if (v->length > 0 && target > v->length - 1) target = v->length - 1;
    if (target < 0) target = 0;
    int64_t ts = v->start + (int64_t)(target / v->tb);
    if (av_seek_frame(v->fmt, v->vidx, ts, AVSEEK_FLAG_BACKWARD) < 0) return;
    avcodec_flush_buffers(v->dec);
    v->flushed = false;
    // Decode forward from the keyframe to the requested time.
    while (next_frame(v) > 0) {
        double pts = frame_pts(v, v->frm);
        if (pts >= target - v->dur / 2) {
            keep_last(v);
            v->pos = pts;
            return;
        }
        av_frame_unref(v->frm);
    }
}

//====================================
// Control overlay (shown while paused)
//------------------------------------
enum { B_PREV, B_BACK, B_PLAY, B_FWD, B_NEXT, NBTN };

typedef struct {
    Rect top, title, od, close;
    Rect bottom, t_cur, t_len, bar, bar_hit, btn[NBTN];
} Layout;

static Layout layout(int w, int h) {
    Layout L;
    L.top = (Rect){ 0, 0, w, 120 };
    L.close = (Rect){ w - 120, 0, 120, 120 };
    L.od = (Rect){ w - 120 - 200, 18, 180, 84 };
    L.title = (Rect){ 36, 0, L.od.x - 60, 120 };
    int by = h - 300;
    L.bottom = (Rect){ 0, by, w, 300 };
    L.t_cur = (Rect){ 50, by + 20, 300, 60 };
    L.t_len = (Rect){ w - 350, by + 20, 300, 60 };
    L.bar = (Rect){ 50, by + 95, w - 100, 18 };
    L.bar_hit = (Rect){ 20, L.bar.y - 45, w - 40, L.bar.h + 90 };
    int bw = 150, bh = 110, gap = 30, x0 = (w - (NBTN * bw + (NBTN - 1) * gap)) / 2;
    for (int i = 0; i < NBTN; i++) L.btn[i] = (Rect){ x0 + i * (bw + gap), by + 160, bw, bh };
    return L;
}

static void draw_overlay(Video *v, const char *name, bool has_prev, bool has_next) {
    const Canvas *c = &v->cv;
    Layout L = layout(c->w, c->h);
    char cur[32], len[32];

    ui_fill(c, L.top, WHITE);
    ui_fill(c, (Rect){ 0, L.top.h - 3, c->w, 3 }, BLACK);
    ui_text_in(c, &font_large, L.title, name, 0, BLACK);
    static const char *const od_label[] = { "OD: off", "OD: on", "OD: max" };
    ui_button(c, L.od, 0, od_label[opt.overdrive]);
    ui_icon(c, L.close, ICON_CLOSE, BLACK);

    ui_fill(c, L.bottom, WHITE);
    ui_fill(c, (Rect){ 0, L.bottom.y, c->w, 3 }, BLACK);
    fmt_time(cur, sizeof cur, v->pos);
    if (v->length > 0) fmt_time(len, sizeof len, v->length);
    else snprintf(len, sizeof len, "--:--");
    ui_text_in(c, &font_small, L.t_cur, cur, 0, BLACK);
    ui_text_in(c, &font_small, L.t_len, len, 2, BLACK);

    double frac = v->length > 0 ? v->pos / v->length : 0;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    int filled = (int)(frac * L.bar.w);
    ui_fill(c, L.bar, LIGHT);
    ui_fill(c, (Rect){ L.bar.x, L.bar.y, filled, L.bar.h }, BLACK);
    ui_frame(c, L.bar, 2, BLACK);
    ui_fill(c, (Rect){ L.bar.x + filled - 7, L.bar.y - 18, 14, L.bar.h + 36 }, BLACK);   // knob

    if (has_prev) ui_button(c, L.btn[B_PREV], ICON_PREV, NULL);
    ui_button(c, L.btn[B_BACK], 0, "-10s");
    ui_button(c, L.btn[B_PLAY], ICON_PLAY, NULL);
    ui_button(c, L.btn[B_FWD], 0, "+10s");
    if (has_next) ui_button(c, L.btn[B_NEXT], ICON_NEXT, NULL);
}

// Video frame (or white) under the overlay, then the overlay.
static void compose(Video *v, const char *name, bool has_prev, bool has_next, bool overlay) {
    ui_fill(&v->cv, (Rect){ 0, 0, v->cv.w, v->cv.h }, WHITE);
    if (v->last->data[0]) draw_frame(v, v->last);
    if (overlay) draw_overlay(v, name, has_prev, has_next);
}

static PlayResult pause_ui(App *app, Video *v, const char *name, bool has_prev, bool has_next) {
    Rect full = { 0, 0, v->cv.w, v->cv.h };
    audio_halt(v->au);
    enum present_mode mode = PRESENT_FLASH;   // first draw cleans up A2 ghosting
    for (;;) {
        compose(v, name, has_prev, has_next, true);
        canvas_present(&app->scr, &v->cv, full, mode);
        mode = PRESENT_UI;

        int fx, fy, x, y;
        Layout L = layout(v->cv.w, v->cv.h);
        for (;;) {
            if (!input_tap(-1, &fx, &fy)) return PL_CLOSE;   // only returns false on SIGTERM etc.
            canvas_from_fb(&v->cv, fx, fy, &x, &y);
            if (rect_hit(L.close, x, y)) return PL_CLOSE;
            if (rect_hit(L.od, x, y)) { opt.overdrive = (opt.overdrive + 1) % 3; break; }   // off -> on -> max
            if (has_prev && rect_hit(L.btn[B_PREV], x, y)) return PL_PREV;
            if (has_next && rect_hit(L.btn[B_NEXT], x, y)) return PL_NEXT;
            if (rect_hit(L.btn[B_BACK], x, y)) { seek_to(v, v->pos - 10); break; }
            if (rect_hit(L.btn[B_FWD], x, y)) { seek_to(v, v->pos + 10); break; }
            if (rect_hit(L.bar_hit, x, y) && v->length > 0) {
                double frac = (double)(x - L.bar.x) / L.bar.w;
                seek_to(v, (frac < 0 ? 0 : frac > 1 ? 1 : frac) * v->length);
                break;
            }
            if (rect_hit(L.btn[B_PLAY], x, y) || (y > L.top.h && y < L.bottom.y)) {
                compose(v, name, has_prev, has_next, false);
                canvas_present(&app->scr, &v->cv, full, PRESENT_FLASH);
                return PL_CONTINUE;
            }
            // tap on empty overlay space: ignore
        }
    }
}

void player_message(App *app, const char *title, const char *msg, double secs) {
    Canvas c;
    canvas_init(&c, &app->scr, app->canvas_buf, false);
    ui_fill(&c, (Rect){ 0, 0, c.w, c.h }, WHITE);
    ui_text_in(&c, &font_large, (Rect){ 40, c.h / 2 - 120, c.w - 80, 80 }, title, 1, BLACK);
    ui_text_in(&c, &font_small, (Rect){ 40, c.h / 2 - 20, c.w - 80, 60 }, msg, 1, BLACK);
    ui_text_in(&c, &font_small, (Rect){ 40, c.h / 2 + 80, c.w - 80, 60 }, "Tap to continue", 1, LIGHT - 0x50);
    canvas_present(&app->scr, &c, (Rect){ 0, 0, c.w, c.h }, PRESENT_FLASH);
    int x, y;
    input_tap((int)(secs * 1000), &x, &y);
}

static void close_video(Video *v) {
    scaler_free(&v->sc);
    audio_close(v->au);
    sws_freeContext(v->sws);
    av_frame_free(&v->frm);
    av_frame_free(&v->last);
    av_packet_free(&v->pkt);
    avcodec_free_context(&v->dec);
    avformat_close_input(&v->fmt);
}

static int open_video(App *app, Video *v, const char *path, const char **why) {
    int err;
    if ((err = avformat_open_input(&v->fmt, path, NULL, NULL)) < 0 ||
        (err = avformat_find_stream_info(v->fmt, NULL)) < 0) {
        *why = "Can't read this file";
        fprintf(stderr, "gmplay: %s: %s\n", path, av_err2str(err));
        return -1;
    }
    const AVCodec *codec = NULL;
    v->vidx = av_find_best_stream(v->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (v->vidx < 0) {
        *why = v->vidx == AVERROR_DECODER_NOT_FOUND ? "Video codec not supported" : "No video stream";
        return -1;
    }
    v->st = v->fmt->streams[v->vidx];
    for (unsigned i = 0; i < v->fmt->nb_streams; i++)
        if ((int)i != v->vidx) v->fmt->streams[i]->discard = AVDISCARD_ALL;   // video only, for now

    v->dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(v->dec, v->st->codecpar);
    v->dec->thread_count = 0;
    v->dec->flags2 |= AV_CODEC_FLAG2_FAST;
    if ((err = avcodec_open2(v->dec, codec, NULL)) < 0) {
        *why = "Can't open video decoder";
        return -1;
    }
    v->pkt = av_packet_alloc();
    v->frm = av_frame_alloc();
    v->last = av_frame_alloc();

    v->tb = av_q2d(v->st->time_base);
    v->start = v->st->start_time != AV_NOPTS_VALUE ? v->st->start_time : 0;
    v->dur = v->st->avg_frame_rate.num > 0 ? 1.0 / av_q2d(v->st->avg_frame_rate) : 1.0 / 25;
    v->length = v->fmt->duration > 0 ? (double)v->fmt->duration / AV_TIME_BASE
              : v->st->duration > 0 ? v->st->duration * v->tb : 0;

    // Fit the display aspect into the screen; rotate landscape video onto a
    // portrait screen (or as forced by -r).
    const Screen *s = &app->scr;
    AVRational sar = av_guess_sample_aspect_ratio(v->fmt, v->st, NULL);
    double sw = v->dec->width * (sar.num > 0 ? av_q2d(sar) : 1.0), sh = v->dec->height;
    bool rot = opt.rotate == ROT_ON || (opt.rotate == ROT_AUTO && (sw > sh) != (s->w > s->h));
    canvas_init(&v->cv, s, app->canvas_buf, rot);
    double k = (double)v->cv.w / sw < (double)v->cv.h / sh ? (double)v->cv.w / sw : (double)v->cv.h / sh;
    int ow = (int)(sw * k + 0.5), oh = (int)(sh * k + 0.5);
    if (ow < 1) ow = 1;
    if (oh < 1) oh = 1;
    v->vr = (Rect){ (v->cv.w - ow) / 2, (v->cv.h - oh) / 2, ow, oh };
    if (opt.verbose)
        fprintf(stderr, "gmplay: %s: %s %dx%d -> %dx%d%s, %.2f fps, %.1fs\n", path, codec->name,
                v->dec->width, v->dec->height, ow, oh, rot ? " (rotated)" : "", 1.0 / v->dur, v->length);
    v->au = audio_open(path);
    return 0;
}

// Current media time: the audio position when there's sound (video follows
// audio), else wall clock. *t0 tracks wall-time-of-pts-0 so we can fall back
// seamlessly if audio stops.
static double media_time(Video *v, double *t0) {
    double c = audio_clock(v->au);
    if (c >= 0) {
        *t0 = now_sec() - c;
        return c;
    }
    return now_sec() - *t0;
}

PlayResult player_run(App *app, const char *path, bool has_prev, bool has_next) {
    Video v;
    memset(&v, 0, sizeof v);
    const char *why = NULL, *name = base_name(path);
    if (open_video(app, &v, path, &why) < 0) {
        close_video(&v);
        player_message(app, name, why, 5);
        return PL_ERROR;
    }

    Screen *scr = &app->scr;
    Rect full = { 0, 0, v.cv.w, v.cv.h };
    ui_fill(&v.cv, full, WHITE);
    canvas_present(scr, &v.cv, full, PRESENT_FLASH);
    // Optional kdc output (-K): owns the panel while video plays; closed for
    // the pause overlay (FBInk) and reopened on resume.
    KdcOut *ko = opt.kdc_preset ? kdcout_open(scr, opt.kdc_preset, opt.gray, opt.verbose) : NULL;

    PlayResult res = PL_END;
    double t0 = now_sec(), last_show = 0, last_start = 0, last_clean = now_sec(), t_start = now_sec();
    bool skipping = false, tapped = false;
    audio_play(v.au, 0);
    int tx, ty;
    for (;;) {
        if (g_stop) { res = PL_CLOSE; break; }
        if (tapped || input_tap(0, &tx, &ty)) {
            tapped = false;
            if (ko) { kdcout_close(ko, opt.verbose); ko = NULL; }
            PlayResult r = pause_ui(app, &v, name, has_prev, has_next);
            if (r != PL_CONTINUE) { res = r; break; }
            if (opt.kdc_preset) ko = kdcout_open(scr, opt.kdc_preset, opt.gray, opt.verbose);
            t0 = now_sec() - v.pos;   // resume from the frame on screen
            audio_play(v.au, v.pos);
            last_show = last_start = last_clean = now_sec();
            continue;
        }

        double t = now_sec();
        int r = next_frame(&v);
        v.t_dec += now_sec() - t;
        if (r == 0) break;
        if (r < 0) { res = PL_ERROR; break; }
        double pts = frame_pts(&v, v.frm);
        if (!v.au && !v.shown && !v.dropped) t0 = now_sec() - pts;   // no audio: first frame is "now"

        // Decoding alone can't keep up: decode less.
        if (!skipping && v.decoded >= 30 && v.t_dec / v.decoded > v.dur * 0.8) {
            skipping = true;
            v.dec->skip_frame = AVDISCARD_NONREF;
            v.dec->skip_loop_filter = AVDISCARD_ALL;
            if (opt.verbose) fprintf(stderr, "gmplay: decoder too slow, skipping non-reference frames\n");
        }

        // Drop the frame if the next one is already due (unless the screen has
        // been static too long), or if it would exceed the -f cap.
        t = now_sec();
        double m = media_time(&v, &t0), wait = pts - m;
        double cap = opt.max_fps > 0 ? opt.max_fps : opt.overdrive == OD_ON ? OD_ON_FPS : 0;
        bool late = m > pts + v.dur && t - last_show < 0.5;
        // cap: time between the starts of shown frames, with half a frame of
        // slack so e.g. 8 fps of 30 fps video isn't rounded down to 7.5
        bool capped = cap > 0 && v.shown && t + (wait > 0 ? wait : 0) - last_start < 1.0 / cap - v.dur / 2;
        if (late || capped) {
            v.dropped++;
            av_frame_unref(v.frm);
            continue;
        }
        while (!g_stop && !tapped) {   // wait until due, still reacting to taps
            int left = (int)((pts - media_time(&v, &t0)) * 1000);
            if (left <= 0) break;
            tapped = input_tap(left < 20 ? left : 20, &tx, &ty);
        }

        // kdc still holds the previous frame at this one's due time: drop it
        // before spending the single CPU core on scaling it. (Checked here,
        // not before waiting: kdc usually takes its frame during the wait.)
        if (ko && kdcout_busy(ko) && !(opt.clean_secs > 0 && now_sec() - last_clean >= opt.clean_secs)) {
            v.dropped++;
            av_frame_unref(v.frm);
            continue;
        }

        t = last_start = now_sec();
        bool flash = opt.clean_secs > 0 && t - last_clean >= opt.clean_secs;
        enum present_mode mode = flash ? PRESENT_FLASH : PRESENT_VIDEO;
        double t1;
        bool limited = v.frm->color_range != AVCOL_RANGE_JPEG && v.frm->format != AV_PIX_FMT_YUVJ420P &&
                       v.frm->format != AV_PIX_FMT_YUVJ422P && v.frm->format != AV_PIX_FMT_YUVJ444P;
        bool fast = !opt.slow_scale && scaler_supports(v.frm->format);
        if (ko) {
            // kdc: plain gray into kdc's frame buffer, no waiting - kdc dithers,
            // drives only changed pixels and drops what the panel can't take.
            int kstride;
            uint8_t *kbuf = kdcout_begin(ko, &kstride);
            Screen ks = *scr;
            ks.fb = kbuf;
            ks.stride = (uint32_t)kstride;
            ks.bpp = 8;
            Rect fr;
            t1 = now_sec();
            if (fast && scaler_setup(&v.sc, &ks, &v.cv, v.vr, v.frm->width, v.frm->height,
                                     v.frm->linesize[0], limited)) {
                v.sc.raw = true;
                scaler_draw(&v.sc, &ks, v.frm->data[0], v.frm->linesize[0]);
                fr = v.sc.fr;
            } else {
                draw_frame(&v, v.frm);
                canvas_blit_raw(scr, &v.cv, v.vr, kbuf, kstride);
                fr = canvas_rect_to_fb(&v.cv, v.vr);
            }
            double t2 = now_sec();
            kdcout_publish(ko, fr);
            if (flash) kdcout_clean(ko);
            v.t_scale += t2 - t1;
            v.t_show += now_sec() - t2;
        } else if (scr->bpp == 8 && fast &&
            scaler_setup(&v.sc, scr, &v.cv, v.vr, v.frm->width, v.frm->height, v.frm->linesize[0], limited)) {
            v.sc.raw = false;
            // Fused scale + dither + rotate into a back buffer while the panel
            // is still busy with the previous frame, then copied over (~2 ms)
            // once it's safe (see canvas_begin).
            Screen back = *scr;
            bool direct = !app->back_buf;
            if (direct) canvas_begin(scr, mode);
            else back.fb = app->back_buf;
            t1 = now_sec();
            scaler_draw(&v.sc, &back, v.frm->data[0], v.frm->linesize[0]);
            double t2 = now_sec();
            v.t_scale += t2 - t1;
            if (!direct) {
                canvas_begin(scr, mode);
                Rect f = v.sc.fr;
                for (int y = f.y; y < f.y + f.h; y++)
                    memcpy(scr->fb + (size_t)y * scr->stride + f.x, back.fb + (size_t)y * scr->stride + f.x, (size_t)f.w);
            }
            canvas_refresh(scr, &v.cv, v.vr, mode);
            v.t_show += (t1 - t) + (now_sec() - t2);   // waiting + copy + refresh
        } else {
            draw_frame(&v, v.frm);   // swscale into the canvas, then dither/rotate
            t1 = now_sec();
            canvas_present(scr, &v.cv, v.vr, mode);
            v.t_scale += t1 - t;
            v.t_show += now_sec() - t1;
        }
        if (flash) last_clean = t;
        last_show = now_sec();
        v.pos = pts;
        v.shown++;
        keep_last(&v);
    }

    if (ko) kdcout_close(ko, opt.verbose);
    double secs = now_sec() - t_start;
    printf("%s: %u frames shown, %u dropped, %.1f fps\n", name, v.shown, v.dropped,
           secs > 0 ? v.shown / secs : 0.0);
    if (opt.verbose && v.decoded && v.shown)
        fprintf(stderr, "gmplay: avg ms: decode %.1f/frame, draw %.1f + panel wait/refresh %.1f per shown frame\n",
                1000 * v.t_dec / v.decoded, 1000 * v.t_scale / v.shown, 1000 * v.t_show / v.shown);
    close_video(&v);
    return res;
}

//====================================
// Legacy .gmv stream (1072x1448, 1 bit/px LSB-first, 1 = white, 130ms/frame)
// Tap to stop.
//------------------------------------
#define GMV_W 1072
#define GMV_H 1448
PlayResult player_run_gmv(App *app, const char *path) {
    FILE *f = strcmp(path, "-") ? fopen(path, "rb") : stdin;
    if (!f) {
        player_message(app, base_name(path), strerror(errno), 5);
        return PL_ERROR;
    }
    Canvas c;
    canvas_init(&c, &app->scr, app->canvas_buf, false);
    Rect full = { 0, 0, c.w, c.h };
    ui_fill(&c, full, WHITE);
    canvas_present(&app->scr, &c, full, PRESENT_FLASH);
    int w = GMV_W < c.w ? GMV_W : c.w, h = GMV_H < c.h ? GMV_H : c.h;
    Rect r = { (c.w - w) / 2, (c.h - h) / 2, w, h };
    static uint8_t packed[GMV_W / 8 * GMV_H];
    PlayResult res = PL_END;
    double t0 = now_sec(), pts = 0;
    int x, y;
    while (fread(packed, sizeof packed, 1, f) == 1) {
        if (g_stop) { res = PL_CLOSE; break; }
        if (input_tap(0, &x, &y)) { res = PL_CLOSE; break; }
        if (now_sec() <= t0 + pts + 0.13) {   // else drop: running late
            for (int yy = 0; yy < h; yy++) {
                uint8_t *dst = c.px + (size_t)(r.y + yy) * c.w + r.x;
                for (int xx = 0; xx < w; xx++)
                    dst[xx] = (packed[(yy * GMV_W + xx) / 8] >> (xx & 7) & 1) ? WHITE : BLACK;
            }
            while (now_sec() < t0 + pts && !g_stop)
                if (input_tap((int)((t0 + pts - now_sec()) * 1000) + 1, &x, &y)) { res = PL_CLOSE; goto out; }
            canvas_present(&app->scr, &c, r, PRESENT_VIDEO);
        }
        pts += 0.13;
    }
out:
    if (f != stdin) fclose(f);
    return res;
}
