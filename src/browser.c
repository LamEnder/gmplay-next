// File browser: a paged list of the videos in a folder.
#include "browser.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "input.h"
#include "ui.h"

#define HEADER_H 130
#define FOOTER_H 150
#define ROW_H 120
#define TRASH_W 110

static const char *base_name(const char *p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

// Per-row hit areas.
static Rect row_trash(const Canvas *c, int y) { return (Rect){ c->w - TRASH_W - 30, y + 10, TRASH_W, ROW_H - 20 }; }

// Modal "Delete <name>?" dialog over the list. Returns true if confirmed.
static bool confirm_delete(App *app, const Canvas *c, const char *path) {
    Rect box = { 60, c->h / 2 - 230, c->w - 120, 460 };
    Rect cancel = { box.x + 50, box.y + box.h - 150, (box.w - 150) / 2, 110 };
    Rect del = { cancel.x + cancel.w + 50, cancel.y, cancel.w, cancel.h };
    ui_fill(c, box, WHITE);
    ui_frame(c, box, 5, BLACK);
    ui_text_in(c, &font_large, (Rect){ box.x + 40, box.y + 30, box.w - 80, 80 }, "Delete this video?", 1, BLACK);
    ui_text_in(c, &font_small, (Rect){ box.x + 40, box.y + 120, box.w - 80, 60 }, base_name(path), 1, BLACK);
    ui_text_in(c, &font_small, (Rect){ box.x + 40, box.y + 180, box.w - 80, 60 },
               "The file is removed from the Kindle.", 1, 0x60);
    ui_button(c, cancel, 0, "Cancel");
    ui_fill(c, del, BLACK);   // destructive action: filled button, white label
    ui_text_in(c, &font_small, (Rect){ del.x + 6, del.y, del.w - 12, del.h }, "Delete", 1, WHITE);
    canvas_present(&app->scr, c, box, PRESENT_UI);
    for (;;) {
        int fx, fy, x, y;
        if (!input_tap(-1, &fx, &fy)) return false;   // SIGTERM etc.
        canvas_from_fb(c, fx, fy, &x, &y);
        if (rect_hit(del, x, y)) return true;
        if (rect_hit(cancel, x, y) || !rect_hit(box, x, y)) return false;
    }
}

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
            char size[32];
            fmt_size(size, sizeof size, path);
            int y = HEADER_H + i * ROW_H;
            Rect trash = row_trash(&c, y);
            ui_text_in(&c, &font_large, (Rect){ 40, y, trash.x - 270, ROW_H }, base_name(path), 0, BLACK);
            ui_text_in(&c, &font_small, (Rect){ trash.x - 220, y, 190, ROW_H }, size, 2, BLACK);
            ui_icon(&c, trash, ICON_TRASH, BLACK);
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
            if (!input_tap(-1, &fx, &fy)) return BROWSER_EXIT;   // SIGTERM etc.
            canvas_from_fb(&c, fx, fy, &x, &y);
            if (rect_hit(close, x, y)) return BROWSER_EXIT;
            if (pages > 1 && page > 0 && rect_hit(prev, x, y)) { page--; break; }
            if (pages > 1 && page < pages - 1 && rect_hit(next, x, y)) { page++; break; }
            if (y >= HEADER_H && y < c.h - FOOTER_H) {
                int row = (y - HEADER_H) / ROW_H, idx = page * per_page + row;
                if (row < per_page && idx < n) {
                    *page_io = page;
                    if (rect_hit(row_trash(&c, HEADER_H + row * ROW_H), x, y)) {
                        if (!confirm_delete(app, &c, files[idx])) break;   // cancelled: redraw the page
                        if (unlink(files[idx]) < 0) {
                            fprintf(stderr, "gmplay: delete %s: %s\n", files[idx], strerror(errno));
                            player_message(app, "Couldn't delete", strerror(errno), 5);
                            break;
                        }
                        printf("deleted %s\n", files[idx]);
                        return BROWSER_RELOAD;
                    }
                    return idx;
                }
            }
        }
    }
}
