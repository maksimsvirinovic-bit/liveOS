#include "path.h"
#include "str.h"
int path_normalize(const char* cwd, const char* input, char* out, uint32_t out_size) {
    if (!input || !out || out_size == 0) return -1;
    char full[512];
    if (input[0] == '/') {
        strncpy(full, input, sizeof(full) - 1);
        full[sizeof(full) - 1] = '\0';
    } else {
        strncpy(full, cwd, sizeof(full) - 1);
        full[sizeof(full) - 1] = '\0';
        int len = strlen(full);
        if (len > 0 && full[len - 1] != '/') {
            full[len++] = '/';
            full[len] = '\0';
        }
        strncat(full, input, sizeof(full) - len - 1);
    }
    const char* components[128];
    int comp_count = 0;
    char* p = full;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char* start = p;
        while (*p && *p != '/') p++;
        char* next = p;
        if (*next == '/') next++;
        *p = '\0';
        if (strcmp(start, ".") == 0) {
        } else if (strcmp(start, "..") == 0) {
            if (comp_count > 0) comp_count--;
        } else {
            if (comp_count < 128) components[comp_count++] = start;
        }
        p = next;
    }
    uint32_t pos = 0;
    out[pos++] = '/';
    for (int i = 0; i < comp_count; i++) {
        int n = strlen(components[i]);
        for (int j = 0; j < n && pos < out_size - 2; j++) {
            out[pos++] = components[i][j];
        }
        if (i < comp_count - 1 && pos < out_size - 1) out[pos++] = '/';
    }
    out[pos] = '\0';
    if (pos == 1 && out[0] == '/') {
        out[1] = '\0';
    }

    return 0;
}