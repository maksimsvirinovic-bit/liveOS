#include "sys.h"
#include "desktop.h"
#include <stddef.h>

static int str_eq(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int str_has_prefix(const char* s, const char* p) {
    while (*p) { if (*s++ != *p++) return 0; }
    return 1;
}

static void spawn_elf(const char* path, const char* args) {
    if (!path || !path[0]) return;
    int pid = sys_fork();
    if (pid == 0) {
        char* argv[4];
        int ac = 0;
        argv[ac++] = (char*)path;
        char argbuf[128];
        if (args && args[0]) {
            uint32_t n = 0;
            while (args[n] && n + 1 < sizeof(argbuf)) { argbuf[n] = args[n]; n++; }
            argbuf[n] = '\0';
            argv[ac++] = argbuf;
        }
        argv[ac] = NULL;
        char* envp[] = { NULL };
        sys_exec(path, argv, envp);
        sys_exit(127);
    }
}

void app_launch(void* ctx, const char* action, const char* args) {
    DesktopState* d = (DesktopState*)ctx;
    (void)d;
    if (!action || !action[0]) return;

    if (str_has_prefix(action, "@builtin:")) {
        const char* name = action + 9;
        if (str_eq(name, "terminal"))      { terminal_open(); return; }
        if (str_eq(name, "ui_studio"))     { ui_studio_open(); return; }
        return;
    }

    if (action[0] == '/') { spawn_elf(action, args); return; }
}