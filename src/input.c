#include "input.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MAX_DEV 4
static int fds[MAX_DEV], nfds;
static struct input_absinfo ax, ay;
static uint32_t sw, sh;
static bool t_swap, t_mx, t_my;

// Touch state for slot 0 (protocol B) / single touch.
static int slot, raw_x, raw_y;
static bool down, lifted;

#define BIT_SET(arr, bit) ((arr)[(bit) / (8 * sizeof(long))] & (1UL << ((bit) % (8 * sizeof(long)))))

int input_open(uint32_t screen_w, uint32_t screen_h) {
    sw = screen_w;
    sh = screen_h;
    const char *env = getenv("GMPLAY_TOUCH");
    if (env) {
        t_swap = strstr(env, "swap") != NULL;
        t_mx = strstr(env, "mirror_x") != NULL;
        t_my = strstr(env, "mirror_y") != NULL;
    }
    DIR *d = opendir("/dev/input");
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d)) && nfds < MAX_DEV) {
        if (strncmp(e->d_name, "event", 5)) continue;
        char path[300];
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        unsigned long bits[(ABS_MAX + 1) / (8 * sizeof(long)) + 1] = {0};
        bool mt = false;
        if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof bits), bits) >= 0 &&
            ((mt = BIT_SET(bits, ABS_MT_POSITION_X)) || BIT_SET(bits, ABS_X))) {
            ioctl(fd, EVIOCGABS(mt ? ABS_MT_POSITION_X : ABS_X), &ax);
            ioctl(fd, EVIOCGABS(mt ? ABS_MT_POSITION_Y : ABS_Y), &ay);
            ioctl(fd, EVIOCGRAB, 1);
            fds[nfds++] = fd;
            if (opt.verbose)
                fprintf(stderr, "gmplay: touch %s x %d..%d y %d..%d%s%s%s\n", path, ax.minimum, ax.maximum,
                        ay.minimum, ay.maximum, t_swap ? " swap" : "", t_mx ? " mirror_x" : "", t_my ? " mirror_y" : "");
        } else {
            close(fd);
        }
    }
    closedir(d);
    return nfds ? 0 : -1;
}

void input_release(void) {
    for (int i = 0; i < nfds; i++) ioctl(fds[i], EVIOCGRAB, 0);
}

void input_close(void) {
    input_release();
    for (int i = 0; i < nfds; i++) close(fds[i]);
    nfds = 0;
}

static int scale(int v, const struct input_absinfo *a, uint32_t size) {
    int range = a->maximum - a->minimum;
    if (range <= 0) return v;
    long r = (long)(v - a->minimum) * (long)(size - 1) / range;
    return r < 0 ? 0 : r >= (long)size ? (int)size - 1 : (int)r;
}

static void map(int *x, int *y) {
    int rx = raw_x, ry = raw_y;
    if (t_swap) {   // panel axes swapped relative to the framebuffer
        *x = scale(ry, &ay, sw);
        *y = scale(rx, &ax, sh);
    } else {
        *x = scale(rx, &ax, sw);
        *y = scale(ry, &ay, sh);
    }
    if (t_mx) *x = (int)sw - 1 - *x;
    if (t_my) *y = (int)sh - 1 - *y;
}

static void handle(const struct input_event *ev) {
    if (ev->type == EV_ABS) {
        switch (ev->code) {
        case ABS_MT_SLOT: slot = ev->value; break;
        case ABS_MT_TRACKING_ID:
            if (slot == 0) {
                if (ev->value >= 0) down = true;
                else if (down) { down = false; lifted = true; }
            }
            break;
        case ABS_MT_POSITION_X: if (slot == 0) raw_x = ev->value; break;
        case ABS_MT_POSITION_Y: if (slot == 0) raw_y = ev->value; break;
        case ABS_X: raw_x = ev->value; break;
        case ABS_Y: raw_y = ev->value; break;
        }
    } else if (ev->type == EV_KEY && ev->code == BTN_TOUCH) {
        if (ev->value) down = true;
        else if (down) { down = false; lifted = true; }
    }
}

bool input_tap(int timeout_ms, int *x, int *y) {
    struct pollfd p[MAX_DEV];
    for (int i = 0; i < nfds; i++) p[i] = (struct pollfd){ fds[i], POLLIN, 0 };
    double deadline = now_sec() + timeout_ms / 1000.0;
    for (;;) {
        int left = timeout_ms < 0 ? 200 : (int)((deadline - now_sec()) * 1000);
        if (left < 0) left = 0;
        if (left > 200) left = 200;   // wake up regularly to notice signals
        int n = poll(p, (nfds_t)nfds, left);
        if (n > 0) {
            for (int i = 0; i < nfds; i++) {
                if (!(p[i].revents & POLLIN)) continue;
                struct input_event ev[32];
                ssize_t r;
                while ((r = read(fds[i], ev, sizeof ev)) > 0) {
                    for (size_t k = 0; k < (size_t)r / sizeof ev[0]; k++) {
                        handle(&ev[k]);
                        // Report at the end of the frame the finger lifted in.
                        if (ev[k].type == EV_SYN && ev[k].code == SYN_REPORT && lifted) {
                            lifted = false;
                            map(x, y);
                            return true;
                        }
                    }
                }
            }
        }
        if (g_stop) return false;
        if (timeout_ms >= 0 && now_sec() >= deadline) return false;
    }
}
