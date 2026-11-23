#include "sys.h"
#include "gui_addon/gui.h"

#define TEXT_MAX 900

static void copy_text(char* dst, uint32_t capacity, const char* src) {
    uint32_t i = 0;
    while (src && src[i] && i + 1 < capacity) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

int main(int argc, char** argv, char** envp) {
    (void)argc; (void)argv; (void)envp;

    int window_id = sys_window_create("Notepad", 220, 140, 560, 420);
    if (window_id < 0) {
        for (;;) sys_yield();
    }

    char text[TEXT_MAX];
    char path[128];
    uint32_t i;
    for (i = 0; i < sizeof(text); i++) text[i] = 0;
    copy_text(path, sizeof(path), "/NOTES.TXT");

    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 0,
                            GUI_LABEL, 12, 8, 44, 16, -1, "File");
    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 1,
                            GUI_TEXTFIELD, 54, 4, 300, 24, -1, path);
    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 2,
                            GUI_BUTTON, 366, 3, 74, 26, -1, "Load");
    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 3,
                            GUI_BUTTON, 448, 3, 74, 26, -1, "Save");
    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 4,
                            GUI_TEXTAREA, 10, 36, 528, 330, -1, text);

    for (;;) {
        UserWindowMessage message;
        if (sys_window_recv(&message) == 0) {
            if (message.window_id == window_id &&
                (message.type == USER_WINDOW_MSG_CLOSE ||
                 message.type == USER_WINDOW_MSG_DESTROY)) sys_exit(0);
        }
        sys_yield();
    }
}
