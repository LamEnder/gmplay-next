#pragma once
#include "screen.h"
#include "font.h"

#define WHITE 0xFF
#define BLACK 0x00
#define LIGHT 0xC8

void ui_fill(const Canvas *c, Rect r, uint8_t v);
void ui_frame(const Canvas *c, Rect r, int t, uint8_t v);   // outline, thickness t
int ui_text_width(const Font *f, const char *s);
// Draw text with its baseline at y; returns the advance width.
int ui_text(const Canvas *c, const Font *f, int x, int y, const char *s, uint8_t v);
// Draw text vertically centered in r; align: 0 left, 1 center, 2 right.
// Truncates with "..." to fit r.w.
void ui_text_in(const Canvas *c, const Font *f, Rect r, const char *s, int align, uint8_t v);

enum icon { ICON_PLAY, ICON_PREV, ICON_NEXT, ICON_CLOSE };
void ui_icon(const Canvas *c, Rect r, enum icon icon, uint8_t v);
// Outlined button with an icon or a label (label used when not NULL).
void ui_button(const Canvas *c, Rect r, enum icon icon, const char *label);
