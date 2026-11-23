#include "sys.h"
#include "gui_addon/gui.h"

int main(int argc, char** argv, char** envp) {
    (void)argc; (void)argv; (void)envp;

    int window_id = sys_window_create("Settings", 170, 100, 480, 360);
    if (window_id < 0) {
        for (;;) sys_yield();
    }

    char serial[16] = "0";
    if (sys_reg_get_u32("SYSTEM/logging/serial_enabled") != 0) {
        serial[0] = '1'; serial[1] = '\0';
    }

    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 0,
                            GUI_LABEL, 12, 8, 440, 16, -1,
                            "Desktop settings");
    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 1,
                            GUI_LABEL, 12, 38, 220, 16, -1,
                            "Serial debug output");
    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 2,
                            GUI_TEXTFIELD, 12, 58, 90, 26, -1, serial);
    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 3,
                            GUI_BUTTON, 114, 58, 100, 28, -1, "Apply");

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
