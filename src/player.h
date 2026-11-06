#pragma once
#include "screen.h"

typedef struct {
    Screen scr;
    uint8_t *canvas_buf;   // screen-sized gray8 scratch canvas
    uint8_t *back_buf;     // fb-shaped back buffer: draw the next frame while the panel refreshes
} App;

typedef enum {
    PL_END,      // reached the end of the file
    PL_NEXT,     // user asked for the next file
    PL_PREV,     // user asked for the previous file
    PL_CLOSE,    // user closed the player (or SIGTERM)
    PL_ERROR,    // couldn't play the file
    PL_CONTINUE, // internal: resume playback
} PlayResult;

PlayResult player_run(App *app, const char *path, bool has_prev, bool has_next);
PlayResult player_run_gmv(App *app, const char *path);

// Show a message screen until tapped or `secs` pass.
void player_message(App *app, const char *title, const char *msg, double secs);
