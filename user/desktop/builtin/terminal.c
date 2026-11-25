#include "desktop/desktop.h"
#include "gui_addon/gui.h"
#include "sys.h"
#include <stddef.h>

static int g_term_window = -1;
static int g_term_output_ctrl = -1;
static int g_term_input_ctrl = -1;
static int g_term_pid = -1;
static char g_term_output[4096];   /* накопительный буфер вывода */
static uint32_t g_term_output_len = 0;

static void terminal_shell(void) {
    /* запускается в дочернем процессе */
    char* argv[] = { "/bin/SH.ELF", NULL };
    char* envp[] = { NULL };
    int ret = sys_exec("/bin/SH.ELF", argv, envp);
    /* если exec не удался — выйти */
    sys_exit(ret);
}

static void terminal_on_close(void* ctx) {
    (void)ctx;
    if (g_term_pid > 0) {
        /* не убиваем процесс, просто отвязываемся */
    }
    sys_tty_focus(0);
    g_term_window = -1;
    g_term_output_ctrl = -1;
    g_term_input_ctrl = -1;
    g_term_pid = -1;
    g_term_output_len = 0;
    g_term_output[0] = '\0';
}

static void terminal_on_focus(void* ctx, int focused) {
    (void)ctx;
    sys_tty_focus(focused ? 1 : 0);
}

static void terminal_on_input_action(void* ctx, GuiControl* c) {
    (void)ctx;
    if (!c) return;
    const char* text = c->text ? c->text : "";
    uint32_t len = 0;
    while (text[len]) len++;
    if (len == 0) return;
    /* отправить строку + \n в TTY */
    sys_tty_write(text, len);
    char nl = '\n';
    sys_tty_write(&nl, 1);
    /* очистить поле ввода */
    gui_control_set_text(&g_desktop.gui, g_term_window, g_term_input_ctrl, "");
    GuiControl* input = gui_control_find(&g_desktop.gui, g_term_window, g_term_input_ctrl);
    if (input) input->cursor = 0;
}

void terminal_open(void) {
    if (g_term_window >= 0) {
        /* уже открыт — фокус */
        GuiWindow* w = gui_window_find(&g_desktop.gui, g_term_window);
        if (w) { w->visible = 1; w->minimized = 0; }
        return;
    }

    g_term_window = gui_window_create(&g_desktop.gui, "Terminal", 80, 60, 720, 500);
    if (g_term_window < 0) return;

    /* вывод */
    g_term_output_ctrl = gui_control_add(&g_desktop.gui, g_term_window,
        GUI_TEXTAREA, 8, 8, 704, 420, "");
    GuiControl* out = gui_control_find(&g_desktop.gui, g_term_window, g_term_output_ctrl);
    if (out) out->scroll = 1;

    /* ввод */
    g_term_input_ctrl = gui_control_add(&g_desktop.gui, g_term_window,
        GUI_TEXTFIELD, 8, 436, 704, 24, "");
    gui_control_set_action(&g_desktop.gui, g_term_window, g_term_input_ctrl,
                           terminal_on_input_action, NULL);

    gui_window_set_close_action(&g_desktop.gui, g_term_window, terminal_on_close, NULL);
    gui_window_set_focus_action(&g_desktop.gui, g_term_window, terminal_on_focus, NULL);

    sys_tty_focus(1);

    /* запустить SH.ELF в фоне */
    g_term_pid = sys_fork();
    if (g_term_pid == 0) {
        /* дочерний */
        sys_tty_focus(1);   /* фокус в TTY */
        terminal_shell();
    }
    /* родитель продолжает */
}

void terminal_update(void) {
    if (g_term_window < 0) return;
    GuiWindow* w = gui_window_find(&g_desktop.gui, g_term_window);
    if (!w) { g_term_window = -1; return; }

    /* читаем весь доступный вывод из TTY */
    char chunk[256];
    int n;
    int changed = 0;
    while ((n = sys_tty_read(chunk, sizeof(chunk))) > 0) {
        changed = 1;
        for (int i = 0; i < n; i++) {
            if (g_term_output_len + 1 >= sizeof(g_term_output)) {
                /* сдвиг: удалить первые 1024 байта */
                uint32_t drop = 1024;
                for (uint32_t j = drop; j < g_term_output_len; j++)
                    g_term_output[j - drop] = g_term_output[j];
                g_term_output_len -= drop;
            }
            char c = chunk[i];
            if (c == '\r') continue;   /* \r\n → \n */
            g_term_output[g_term_output_len++] = c;
        }
        g_term_output[g_term_output_len] = '\0';
    }

    if (changed) {
        gui_control_set_text(&g_desktop.gui, g_term_window, g_term_output_ctrl, g_term_output);
        g_desktop.server.pending_redraw = 1;
    }
}

void terminal_close(void) {
    if (g_term_window >= 0) {
        gui_window_destroy(&g_desktop.gui, g_term_window);
    }
}