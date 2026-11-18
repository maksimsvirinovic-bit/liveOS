#include "../../sys.h"
#include "../gui.h"

#define MAX_ENTRIES 32
#define PATH_MAX 192

typedef struct {
    char path[PATH_MAX];
    char names[MAX_ENTRIES][64];
    uint8_t attrs[MAX_ENTRIES];
    uint32_t count;
} FilesState;

static void copy_text(char* dst, uint32_t capacity, const char* src) {
    uint32_t i = 0;
    if (!dst || capacity == 0) return;
    while (src && src[i] && i + 1 < capacity) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int join_path(char* out, uint32_t capacity, const char* base, const char* name) {
    uint32_t i = 0;
    while (base && base[i] && i + 1 < capacity) { out[i] = base[i]; i++; }
    if (i && out[i - 1] != '/' && i + 1 < capacity) out[i++] = '/';
    uint32_t j = 0;
    while (name && name[j] && i + 1 < capacity) out[i++] = name[j++];
    out[i] = '\0';
    return name && name[j] ? -1 : 0;
}

static void refresh_files(FilesState* state, int window_id) {
    for (uint32_t i = 0; i < MAX_ENTRIES; i++) {
        state->names[i][0] = '\0';
        state->attrs[i] = 0;
        sys_window_send_control(USER_WINDOW_MSG_UPDATE_CONTROL, window_id, 5,
                                0, i, 0, 0, 0, i, "");
    }

    state->count = 0;
    int fd = sys_opendir(state->path);
    if (fd < 0) {
        sys_window_send_control(USER_WINDOW_MSG_UPDATE_CONTROL, window_id, 6,
                                0, 0, 0, 0, 0, -1, "Cannot open directory");
        return;
    }

    for (uint32_t i = 0; i < MAX_ENTRIES; i++) {
        DirEntry entry;
        if (sys_readdir(fd, i, &entry) != 0) break;
        copy_text(state->names[i], sizeof(state->names[i]), entry.name);
        state->attrs[i] = entry.attr;
        char display[96];
        if (entry.attr & FS_ATTR_DIR) {
            display[0] = '['; display[1] = 'D'; display[2] = ']'; display[3] = ' ';
            copy_text(display + 4, sizeof(display) - 4, entry.name);
        } else {
            copy_text(display, sizeof(display), entry.name);
        }
        sys_window_send_control(USER_WINDOW_MSG_UPDATE_CONTROL, window_id, 5,
                                0, i, 0, 0, 0, i, display);
        state->count++;
    }
    sys_close(fd);

    sys_window_send_control(USER_WINDOW_MSG_UPDATE_CONTROL, window_id, 6,
                            0, 0, 0, 0, 0, -1, state->path);
}

int main(int argc, char** argv, char** envp) {
    (void)argc; (void)argv; (void)envp;

    FilesState state;
    uint32_t i;
    for (i = 0; i < sizeof(state); i++) ((uint8_t*)&state)[i] = 0;
    copy_text(state.path, sizeof(state.path), "/");

    int window_id = sys_window_create("Files", 140, 90, 620, 470);
    if (window_id < 0) {
        for (;;) sys_yield();
    }

    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 0,
                            GUI_LABEL, 10, 8, 40, 16, -1, "Path");
    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 1,
                            GUI_TEXTFIELD, 50, 4, 310, 24, -1, state.path);
    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 2,
                            GUI_BUTTON, 368, 3, 64, 26, -1, "Open");
    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 3,
                            GUI_BUTTON, 438, 3, 64, 26, -1, "Up");
    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 4,
                            GUI_BUTTON, 508, 3, 72, 26, -1, "Back");
    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 5,
                            GUI_LIST, 10, 38, 590, 358, -1, "");
    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 6,
                            GUI_LABEL, 12, 404, 580, 16, -1, state.path);

    refresh_files(&state, window_id);

    for (;;) {
        UserWindowMessage message;
        if (sys_window_recv(&message) == 0) {
            if (message.window_id == window_id &&
                (message.type == USER_WINDOW_MSG_CLOSE ||
                 (message.type == USER_WINDOW_MSG_DESTROY))) sys_exit(0);

            if (message.type == USER_WINDOW_MSG_INPUT && message.window_id == window_id) {
                if (message.input_type == USER_WINDOW_INPUT_KEY && message.input_key == '\n' &&
                    message.selected >= 0 && message.selected < (int32_t)state.count) {
                    char candidate[PATH_MAX];
                    if (join_path(candidate, sizeof(candidate), state.path, state.names[message.selected]) == 0) {
                        if (state.attrs[message.selected] & FS_ATTR_DIR) {
                            copy_text(state.path, sizeof(state.path), candidate);
                            refresh_files(&state, window_id);
                        }
                    }
                }
            }
        }
        sys_yield();
    }
}
