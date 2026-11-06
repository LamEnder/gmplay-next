// File browser: a paged list of the videos in a folder.
#include "browser.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "input.h"
#include "ui.h"

#define HEADER_H 130
#define FOOTER_H 150
#define ROW_H 120

static void fmt_size(char *buf, size_t n, const char *path) {
    struct stat st;
    if (stat(path, &st) < 0) { snprintf(buf, n, "?"); return; }
    double b = (double)st.st_size;
    if (b >= 1e9) snprintf(buf, n, "%.1f GB", b / 1e9);
    else if (b >= 1e6) snprintf(buf, n, "%.1f MB", b / 1e6);
    else snprintf(buf, n, "%.0f KB", b / 1e3);
}

int browser_run(App *app, const char *dir, char **files, int n, int *page_io) {
    Canvas c;
    canvas_init(&c, &app->scr, app->canvas_buf, false);
    int per_page = (c.h - HEADER_H - FOOTER_H) / ROW_H;
    int pages = n ? (n + per_page - 1) / per_page : 1;
    int page = *page_io < pages ? *page_io : pages - 1;
    Rect close = { c.w - HEADER_H, 0, HEADER_H, HEADER_H };
    Rect prev = { 30, c.h - FOOTER_H + 20, 200, FOOTER_H - 40 };
    Rect next = { c.w - 230, c.h - FOOTER_H + 20, 200, FOOTER_H - 40 };

    for (;;) {
        // draw the page
        ui_fill(&c, (Rect){ 0, 0, c.w, c.h }, WHITE);
        char title[300];
        snprintf(title, sizeof title, "Videos - %s", dir);
        ui_text_in(&c, &font_large, (Rect){ 36, 0, c.w - HEADER_H - 60, HEADER_H }, title, 0, BLACK);
        ui_icon(&c, close, ICON_CLOSE, BLACK);
        ui_fill(&c, (Rect){ 0, HEADER_H - 3, c.w, 3 }, BLACK);
        if (!n) {
            ui_text_in(&c, &font_small, (Rect){ 40, c.h / 2 - 60, c.w - 80, 60 }, "No videos found.", 1, BLACK);
            ui_text_in(&c, &font_small, (Rect){ 40, c.h / 2, c.w - 80, 60 },
                       "Copy .mp4/.mkv/.webm/.avi files into this folder.", 1, BLACK);
        }
        for (int i = 0; i < per_page && page * per_page + i < n; i++) {
            const char *path = files[page * per_page + i];
            const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
            char size[32];
            fmt_size(size, sizeof size, path);
            int y = HEADER_H + i * ROW_H;
            ui_text_in(&c, &font_large, (Rect){ 40, y, c.w - 300, ROW_H }, name, 0, BLACK);
            ui_text_in(&c, &font_small, (Rect){ c.w - 240, y, 200, ROW_H }, size, 2, BLACK);
            ui_fill(&c, (Rect){ 30, y + ROW_H - 2, c.w - 60, 2 }, LIGHT);
        }
        ui_fill(&c, (Rect){ 0, c.h - FOOTER_H, c.w, 3 }, BLACK);
        if (pages > 1) {
            char pg[32];
            snprintf(pg, sizeof pg, "%d / %d", page + 1, pages);
            ui_text_in(&c, &font_small, (Rect){ 0, c.h - FOOTER_H, c.w, FOOTER_H }, pg, 1, BLACK);
            if (page > 0) ui_button(&c, prev, ICON_PREV, NULL);
            if (page < pages - 1) ui_button(&c, next, ICON_NEXT, NULL);
        }
        canvas_present(&app->scr, &c, (Rect){ 0, 0, c.w, c.h }, PRESENT_FLASH);

        // handle taps until something changes
        for (;;) {
            int fx, fy, x, y;
            if (!input_tap(-1, &fx, &fy)) return -1;   // SIGTERM etc.
            canvas_from_fb(&c, fx, fy, &x, &y);
            if (rect_hit(close, x, y)) return -1;
            if (pages > 1 && page > 0 && rect_hit(prev, x, y)) { page--; break; }
            if (pages > 1 && page < pages - 1 && rect_hit(next, x, y)) { page++; break; }
            if (y >= HEADER_H && y < c.h - FOOTER_H) {
                int idx = page * per_page + (y - HEADER_H) / ROW_H;
                if ((y - HEADER_H) / ROW_H < per_page && idx < n) {
                    *page_io = page;
                    return idx;
                }
            }
        }
    }
}
