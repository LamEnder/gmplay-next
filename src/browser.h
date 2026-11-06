#pragma once
#include "player.h"

// Paged list of `files`. Returns the tapped index, or -1 to exit.
// *page is the page to open on, updated to the page the user was on.
int browser_run(App *app, const char *dir, char **files, int n, int *page);
