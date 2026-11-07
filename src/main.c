//====================================================
// gmplay 2.0 - e-ink video player for Kindle
// Based on gmplay 1.5a, Copyright (C) 2012 by geekmaster, MIT license:
// http://www.opensource.org/licenses/mit-license.php
//----------------------------------------------------
// Plays common video files (mp4, mkv, webm, avi, ...) via FFmpeg and legacy
// .gmv streams, with a file browser and a tap-to-pause control overlay.
// Screen refreshes go through FBInk so i.MX and MTK Kindles both work.
// Audio goes to the Kindle's mixer (Bluetooth headphones) via mixersink.
//----------------------------------------------------
#include <dirent.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <libavutil/log.h>

#include "browser.h"
#include "input.h"
#include "player.h"
#include "sys.h"
#include "ui.h"

#define DEFAULT_DIR "/mnt/us/videos"

Options opt = { .rotate = ROT_AUTO, .av_delay = 0.15 };
volatile sig_atomic_t g_stop;

double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

static void on_signal(int sig) { (void)sig; g_stop = 1; }

// On a crash, give the touchscreen back and un-freeze the Kindle UI so the
// device isn't left unresponsive, then die normally.
static void on_crash(int sig) {
    input_release();
    sys_thaw_ui();
    signal(sig, SIG_DFL);
    raise(sig);
}

static bool has_ext(const char *name, const char *const *exts) {
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    for (; *exts; exts++)
        if (!strcasecmp(dot + 1, *exts)) return true;
    return false;
}

static bool is_gmv(const char *path) {
    static const char *const gmv[] = { "gmv", NULL };
    return !strcmp(path, "-") || has_ext(path, gmv);
}

static int cmp_str(const void *a, const void *b) {
    return strcasecmp(*(char *const *)a, *(char *const *)b);
}

// Sorted full paths of the videos in dir.
static int list_videos(const char *dir, char ***out) {
    static const char *const exts[] = { "mp4", "m4v", "mov", "mkv", "webm", "avi", "ts", "mpg", "mpeg",
                                        "flv", "ogv", "wmv", "asf", "3gp", "gmv", NULL };
    char **v = NULL;
    int n = 0;
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.' || !has_ext(e->d_name, exts)) continue;
            char **nv = realloc(v, sizeof *v * (size_t)(n + 1));
            if (!nv) break;
            v = nv;
            size_t len = strlen(dir) + strlen(e->d_name) + 2;
            v[n] = malloc(len);
            snprintf(v[n++], len, "%s/%s", dir, e->d_name);
        }
        closedir(d);
    }
    if (n) qsort(v, (size_t)n, sizeof *v, cmp_str);
    *out = v;
    return n;
}

// Play files[i..] until closed or the end of the list.
static void play_list(App *app, char **files, int n, int i) {
    while (i >= 0 && i < n && !g_stop) {
        PlayResult r = is_gmv(files[i]) ? player_run_gmv(app, files[i])
                                        : player_run(app, files[i], i > 0, i < n - 1);
        if (r == PL_CLOSE) return;
        i += r == PL_PREV ? -1 : 1;
    }
}

// -T: draw a dot wherever the screen is tapped, to check touch mapping.
static void touch_test(App *app) {
    Canvas c;
    canvas_init(&c, &app->scr, app->canvas_buf, false);
    Rect full = { 0, 0, c.w, c.h }, close = { c.w - 130, 0, 130, 130 };
    ui_fill(&c, full, WHITE);
    ui_text_in(&c, &font_large, (Rect){ 40, c.h / 2 - 80, c.w - 80, 80 }, "Touch test", 1, BLACK);
    ui_text_in(&c, &font_small, (Rect){ 40, c.h / 2, c.w - 80, 60 },
               "A dot should appear under your finger. X to exit.", 1, BLACK);
    ui_icon(&c, close, ICON_CLOSE, BLACK);
    canvas_present(&app->scr, &c, full, PRESENT_FLASH);
    int x, y;
    while (input_tap(-1, &x, &y)) {
        printf("tap %d,%d\n", x, y);
        fflush(stdout);
        if (rect_hit(close, x, y)) break;
        Rect dot = { x - 20, y - 20, 40, 40 };
        ui_fill(&c, dot, BLACK);
        canvas_present(&app->scr, &c, dot, PRESENT_UI);
    }
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "usage: %s [options] [folder | file...]\n"
        "With a folder (default " DEFAULT_DIR "), shows a file browser.\n"
        "With files, plays them in order ('-' = .gmv on stdin).\n"
        "Tap the screen while playing for controls.\n\n"
        "  -r auto|on|off  rotate landscape video 90 deg to fill a portrait screen (default auto)\n"
        "  -g              16-level grayscale video (slower refresh) instead of black/white\n"
        "  -c SECS         flashing refresh every SECS while playing to clear ghosting\n"
        "  -o              overdrive: start the next frame while the panel is still\n"
        "                  refreshing, capped at 10 fps - smoother, occasional glitches\n"
        "  -O              max overdrive: uncapped - highest fps, frequent glitches\n"
        "  -f FPS          cap displayed frames per second (overrides -o's 10 fps cap)\n"
        "  -m              mute (no audio)\n"
        "  -a MS           audio output delay to compensate for, e.g. Bluetooth (default 150)\n"
        "  -S              use the slower generic scaler (for comparison/troubleshooting)\n"
        "  -K PRESET       video through kdc (per-pixel pulse drive) with a calibrated\n"
        "                  preset (.doses); needs the pulse waveform installed\n"
        "  -T              touch test\n"
        "  -v              verbose\n", argv0);
}

int main(int argc, char **argv) {
    bool touch = false;
    int ch;
    while ((ch = getopt(argc, argv, "r:gc:oOf:ma:STK:vh")) != -1) {
        switch (ch) {
        case 'r':
            if (!strcmp(optarg, "auto")) opt.rotate = ROT_AUTO;
            else if (!strcmp(optarg, "on")) opt.rotate = ROT_ON;
            else if (!strcmp(optarg, "off")) opt.rotate = ROT_OFF;
            else { usage(argv[0]); return 2; }
            break;
        case 'g': opt.gray = true; break;
        case 'c': opt.clean_secs = atof(optarg); break;
        case 'o': opt.overdrive = OD_ON; break;
        case 'O': opt.overdrive = OD_MAX; break;
        case 'f': opt.max_fps = atof(optarg); break;
        case 'm': opt.mute = true; break;
        case 'S': opt.slow_scale = true; break;
        case 'a': opt.av_delay = atof(optarg) / 1000.0; break;
        case 'T': touch = true; break;
        case 'K': opt.kdc_preset = optarg; break;
        case 'v': opt.verbose = true; break;
        default: usage(argv[0]); return ch == 'h' ? 0 : 2;
        }
    }
    av_log_set_level(opt.verbose ? AV_LOG_WARNING : AV_LOG_ERROR);

    // Folder (browser) or explicit files?
    const char *dir = NULL;
    char **files = argv + optind;
    int nfiles = argc - optind;
    struct stat st;
    if (nfiles == 0) dir = DEFAULT_DIR;
    else if (nfiles == 1 && stat(argv[optind], &st) == 0 && S_ISDIR(st.st_mode)) dir = argv[optind];

    struct sigaction sa = { .sa_handler = on_signal };
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);   // audio sink going away must not kill us
    int crash[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    for (size_t i = 0; i < sizeof crash / sizeof *crash; i++) signal(crash[i], on_crash);

    App app;
    if (screen_open(&app.scr) < 0) return 1;
    app.canvas_buf = malloc((size_t)app.scr.w * app.scr.h);
    if (!app.canvas_buf) return 1;
    app.back_buf = malloc((size_t)app.scr.stride * app.scr.h);   // optional: NULL just means no pipelining
    if (input_open(app.scr.w, app.scr.h) < 0) fprintf(stderr, "gmplay: no touchscreen found\n");
    screen_save(&app.scr);
    sys_freeze_ui();
    sys_prevent_screensaver(true);

    if (touch) {
        touch_test(&app);
    } else if (dir) {
        int page = 0;
        for (;;) {
            char **list;
            int n = list_videos(dir, &list);   // re-listed each time: picks up deletions
            int sel = browser_run(&app, dir, list, n, &page);
            if (sel >= 0 && !g_stop) play_list(&app, list, n, sel);
            for (int i = 0; i < n; i++) free(list[i]);
            free(list);
            if (sel == BROWSER_EXIT || g_stop) break;
        }
    } else {
        play_list(&app, files, nfiles, 0);
    }

    input_close();
    screen_restore(&app.scr);   // put back what was on screen before
    sys_prevent_screensaver(false);
    sys_thaw_ui();
    screen_close(&app.scr);
    free(app.canvas_buf);
    free(app.back_buf);
    return 0;
}
