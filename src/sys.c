#include "sys.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_FROZEN 32
static pid_t frozen[MAX_FROZEN];
static volatile int nfrozen;

static bool in_list(const char *list, const char *name) {
    size_t n = strlen(name);
    for (const char *p = list; (p = strstr(p, name)); p += n) {
        bool start = p == list || *(p - 1) == ' ';
        bool end = p[n] == 0 || p[n] == ' ';
        if (start && end) return true;
    }
    return false;
}

void sys_freeze_ui(void) {
    const char *list = getenv("GMPLAY_FREEZE");
    if (!list) list = "cvm awesome reader.lua";
    DIR *d = opendir("/proc");
    if (!d) return;
    struct dirent *e;
    pid_t self = getpid();
    while ((e = readdir(d)) && nfrozen < MAX_FROZEN) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        pid_t pid = (pid_t)atoi(e->d_name);
        if (pid == self) continue;
        char path[64], buf[512];
        snprintf(path, sizeof path, "/proc/%d/stat", pid);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[n] = 0;
        // "pid (comm) S ..." - comm may contain spaces/parens, so use the last ')'
        char *l = strchr(buf, '('), *r = strrchr(buf, ')');
        if (!l || !r || r < l || r[1] != ' ') continue;
        *r = 0;
        char state = r[2];
        if (state == 'T' || state == 't' || !in_list(list, l + 1)) continue;
        if (kill(pid, SIGSTOP) == 0) {
            frozen[nfrozen++] = pid;
            if (opt.verbose) fprintf(stderr, "gmplay: paused %s (%d)\n", l + 1, pid);
        }
    }
    closedir(d);
}

void sys_thaw_ui(void) {
    int n = nfrozen;
    nfrozen = 0;
    for (int i = 0; i < n; i++) kill(frozen[i], SIGCONT);
}

static int saved_prevent = -1;

void sys_prevent_screensaver(bool on) {
    if (on) {
        FILE *p = popen("lipc-get-prop com.lab126.powerd preventScreenSaver 2>/dev/null", "r");
        if (p) {
            if (fscanf(p, "%d", &saved_prevent) != 1) saved_prevent = -1;
            pclose(p);
        }
        if (system("lipc-set-prop com.lab126.powerd preventScreenSaver 1 >/dev/null 2>&1")) {}
    } else if (saved_prevent != 1) {   // only undo what we changed
        if (system("lipc-set-prop com.lab126.powerd preventScreenSaver 0 >/dev/null 2>&1")) {}
    }
}
