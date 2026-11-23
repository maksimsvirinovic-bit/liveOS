#include "sys.h"
#include "gui_addon/gui.h"

int main(int argc, char** argv, char** envp) {
    (void)argc; (void)argv; (void)envp;

    int window_id = sys_window_create("Registry", 120, 80, 620, 420);
    if (window_id < 0) {
        for (;;) sys_yield();
    }

    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 0,
                            GUI_TEXTFIELD, 12, 4, 220, 24, -1, "SYSTEM");
    sys_window_send_control(USER_WINDOW_MSG_CREATE_CONTROL, window_id, 1,
                            GUI_LIST, 12, 38, 596, 330, -1, "");

    UserRegEntry entries[32];
    for (uint32_t row = 0; row < 32; row++) {
        if (sys_reg_enum("SYSTEM", row, &entries[row]) != 0) break;
        char line[96];
        uint32_t i = 0;
        while (entries[row].name[i] && i + 1 < sizeof(line)) {
            line[i] = entries[row].name[i]; i++;
        }
        if (!entries[row].is_key) {
            if (i + 3 < sizeof(line)) {
                line[i++] = ' '; line[i++] = '='; line[i++] = ' ';
            }
            uint32_t j = 0;
            while (entries[row].text[j] && i + 1 < sizeof(line)) line[i++] = entries[row].text[j++];
        }
        line[i] = '\0';
        sys_window_send_control(USER_WINDOW_MSG_UPDATE_CONTROL, window_id, 1,
                                GUI_LIST, row, 0, 0, 0, row, line);
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
