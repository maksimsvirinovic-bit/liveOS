#include "sys.h"
#include "gui_addon/gui.h"

int main(int argc, char** argv, char** envp) {
    (void)argc; (void)argv; (void)envp;

    int window_id = sys_window_create("Fonts", 220, 120, 440, 300);
    if (window_id < 0) {
        for (;;) sys_yield();
    }

    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 0,
                            GUI_LABEL, 12, 8, 416, 16, -1,
                            "Font settings are managed by the window client");
    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 1,
                            GUI_TEXTFIELD, 12, 34, 180, 26, -1, "12");
    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 2,
                            GUI_BUTTON, 204, 34, 100, 28, -1, "Apply");
    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 3,
                            GUI_LIST, 12, 74, 416, 180, -1, "");

    int fd = sys_opendir("/usr/fonts");
    if (fd >= 0) {
        for (uint32_t row = 0; row < 16; row++) {
            DirEntry entry;
            if (sys_readdir(fd, row, &entry) != 0) break;
            sys_window_send_control(USER_WINDOW_MSG_UPDATE_CONTROL, window_id, 3,
                                    GUI_LIST, row, 0, 0, 0, row, entry.name);
        }
        sys_close(fd);
    }

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
