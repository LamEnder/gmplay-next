#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Rect clip(const Canvas *c, Rect r) {
    if (r.x < 0) { r.w += r.x; r.x = 0; }
    if (r.y < 0) { r.h += r.y; r.y = 0; }
    if (r.x + r.w > c->w) r.w = c->w - r.x;
    if (r.y + r.h > c->h) r.h = c->h - r.y;
    return r;
}

void ui_fill(const Canvas *c, Rect r, uint8_t v) {
    r = clip(c, r);
    for (int y = r.y; y < r.y + r.h; y++) memset(c->px + (size_t)y * c->w + r.x, v, (size_t)(r.w > 0 ? r.w : 0));
}

void ui_frame(const Canvas *c, Rect r, int t, uint8_t v) {
    ui_fill(c, (Rect){ r.x, r.y, r.w, t }, v);
    ui_fill(c, (Rect){ r.x, r.y + r.h - t, r.w, t }, v);
    ui_fill(c, (Rect){ r.x, r.y, t, r.h }, v);
    ui_fill(c, (Rect){ r.x + r.w - t, r.y, t, r.h }, v);
}

static const Glyph *glyph(const Font *f, char ch) {
    unsigned char u = (unsigned char)ch;
    if (u < FONT_FIRST || u > FONT_LAST) u = '?';
    return &f->g[u - FONT_FIRST];
}

int ui_text_width(const Font *f, const char *s) {
    int w = 0;
    for (; *s; s++) w += glyph(f, *s)->adv;
    return w;
}

int ui_text(const Canvas *c, const Font *f, int x, int y, const char *s, uint8_t v) {
    int x0 = x;
    for (; *s; s++) {
        const Glyph *g = glyph(f, *s);
        const uint8_t *a = f->bits + g->off;
        for (int gy = 0; gy < g->h; gy++) {
            int py = y + g->yoff + gy;
            if (py < 0 || py >= c->h) continue;
            uint8_t *row = c->px + (size_t)py * c->w;
            for (int gx = 0; gx < g->w; gx++) {
                int px = x + g->xoff + gx;
                int al = a[gy * g->w + gx];
                if (px < 0 || px >= c->w || !al) continue;
                row[px] = (uint8_t)((row[px] * (255 - al) + v * al) / 255);   // alpha blend
            }
        }
        x += g->adv;
    }
    return x - x0;
}

void ui_text_in(const Canvas *c, const Font *f, Rect r, const char *s, int align, uint8_t v) {
    char buf[512];
    snprintf(buf, sizeof buf, "%s", s);
    if (ui_text_width(f, buf) > r.w) {   // truncate with an ellipsis
        size_t n = strlen(buf);
        int ell = ui_text_width(f, "...");
        while (n > 0 && ui_text_width(f, buf) + ell > r.w) buf[--n] = 0;
        strcat(buf, "...");
    }
    int w = ui_text_width(f, buf);
    int x = align == 0 ? r.x : align == 1 ? r.x + (r.w - w) / 2 : r.x + r.w - w;
    int y = r.y + (r.h + f->ascent - f->descent) / 2;
    ui_text(c, f, x, y, buf, v);
}

// Filled triangle pointing right (dir=1) or left (dir=-1), centered at cx,cy.
static void triangle(const Canvas *c, int cx, int cy, int size, int dir, uint8_t v) {
    int half = size / 2, w = size * 7 / 8;
    for (int dy = -half; dy <= half; dy++) {
        int len = w * (half - abs(dy)) / half;
        int x0 = dir > 0 ? cx - w / 2 : cx + w / 2 - len;
        ui_fill(c, (Rect){ x0, cy + dy, len, 1 }, v);
    }
}

void ui_icon(const Canvas *c, Rect r, enum icon icon, uint8_t v) {
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2, s = (r.w < r.h ? r.w : r.h) / 2;
    switch (icon) {
    case ICON_PLAY:
        triangle(c, cx + s / 10, cy, s, 1, v);
        break;
    case ICON_PREV:
        ui_fill(c, (Rect){ cx - s / 2, cy - s / 2, s / 7 + 1, s }, v);
        triangle(c, cx + s / 10, cy, s, -1, v);
        break;
    case ICON_NEXT:
        ui_fill(c, (Rect){ cx + s / 2 - s / 7, cy - s / 2, s / 7 + 1, s }, v);
        triangle(c, cx - s / 10, cy, s, 1, v);
        break;
    case ICON_TRASH: {   // lid + handle + bin with two ribs
        int t = s / 12 + 2, bw = s * 7 / 10, bh = s * 9 / 10, bx = cx - bw / 2, by = cy - s * 3 / 8;
        ui_fill(c, (Rect){ cx - s * 9 / 20, by - t * 2, s * 9 / 10, t }, v);           // lid
        ui_frame(c, (Rect){ cx - s / 6, by - t * 2 - s / 7, s / 3, s / 7 + 1 }, t, v);  // handle
        ui_frame(c, (Rect){ bx, by, bw, bh }, t, v);                                    // bin
        ui_fill(c, (Rect){ cx - bw / 6 - t / 2, by + bh / 5, t, bh * 3 / 5 }, v);       // ribs
        ui_fill(c, (Rect){ cx + bw / 6 - t / 2, by + bh / 5, t, bh * 3 / 5 }, v);
        break;
    }
    case ICON_CLOSE: {   // two thick diagonals
        int half = s / 2, t = s / 9 + 2;
        for (int dy = -half; dy <= half; dy++)
            for (int dx = -half; dx <= half; dx++)
                if (abs(dx - dy) <= t / 2 || abs(dx + dy) <= t / 2)
                    ui_fill(c, (Rect){ cx + dx, cy + dy, 1, 1 }, v);
        break;
    }
    }
}

void ui_button(const Canvas *c, Rect r, enum icon icon, const char *label) {
    ui_fill(c, r, WHITE);
    ui_frame(c, r, 3, BLACK);
    if (label) ui_text_in(c, &font_small, (Rect){ r.x + 6, r.y, r.w - 12, r.h }, label, 1, BLACK);
    else ui_icon(c, r, icon, BLACK);
}
