#pragma once
#include "player.h"

#define BROWSER_EXIT   -1   // user closed the browser
#define BROWSER_RELOAD -2   // a file was deleted: re-list the folder and call again

// Paged list of `files`. Returns the tapped index, BROWSER_EXIT or
// BROWSER_RELOAD. *page is the page to open on, updated to the page the
// user was on.
int browser_run(App *app, const char *dir, char **files, int n, int *page);
