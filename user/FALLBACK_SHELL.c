#include "sys.h"

static void putstr(const char* text) {
    while (*text) {
        char ch = *text++;
        sys_write(1, &ch, 1);
    }
}

static void print_prompt(void) {
    putstr("liveos> ");
}

int main(int argc, char** argv, char** envp) {
    (void)argc;
    (void)argv;
    (void)envp;

    char line[128];
    int len = 0;
    putstr("LiveOS fallback shell\n");
    for (;;) {
        print_prompt();
        len = 0;
        while (len < (int)(sizeof(line) - 1)) {
            char ch = 0;
            int got = sys_read(0, &ch, 1);
            if (got <= 0) continue;
            if (ch == '\n' || ch == '\r') {
                putstr("\n");
                break;
            }
            if (ch == '\b') {
                if (len > 0) {
                    len--;
                    putstr("\b \b");
                }
                continue;
            }
            if (ch >= 32 && ch < 127) {
                line[len++] = ch;
                sys_write(1, &ch, 1);
            }
        }
        line[len] = '\0';
        if (len == 0) continue;
        if (len == 4 && line[0] == 'e' && line[1] == 'x' && line[2] == 'i' && line[3] == 't') {
            sys_exit(0);
        }
        putstr("\nUnknown command: ");
        putstr(line);
        putstr("\n");
    }
}
