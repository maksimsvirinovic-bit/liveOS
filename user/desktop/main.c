#include "sys.h"
#include "gui_addon/gui.h"
#include "gui_addon/render.h"
#include "gui_addon/window_server.h"
#include "desktop.h"

DesktopState g_desktop;

#define COLOR_BACKGROUND 0x00151B26u
#define COLOR_ACCENT_DEFAULT 0x0000A7C4u

static int drag_initialized = 0;
static int drag_present_x, drag_present_y;
static int drag_width, drag_height;
static int dragging_id = -1;
static uint32_t last_drag_present = 0;

static void send_window_message(const UserWindowMessage* in) {
    WindowServerMessage req = {0};
    req.type = in->type;
    req.window_id = in->window_id;
    req.owner_pid = in->owner_pid;
    req.x = in->x; req.y = in->y;
    req.width = in->width; req.height = in->height;
    req.control_type = in->control_type;
    req.control_id = in->control_id;
    req.selected = in->selected;
    req.input_type = in->input_type;
    req.input_key = in->input_key;
    req.result = in->result;
    req.payload_size = in->payload_size;
    if (in->payload_size > 0 && in->payload_size < sizeof(req.payload)) {
        for (uint32_t i = 0; i < in->payload_size; i++) req.payload[i] = in->payload[i];
    }
    WindowServerMessage rep = {0};
    int result = window_server_dispatch_message(&g_desktop.server, &req, &rep);

    if (in->type == USER_WINDOW_MSG_CREATE_CONTROL ||
        in->type == USER_WINDOW_MSG_UPDATE_CONTROL) return;
    UserWindowMessage response = {0};
    response.type = rep.type;
    response.window_id = rep.window_id;
    response.control_id = rep.control_id;
    response.result = result;
    response.owner_pid = 0;
    response.x = rep.x; response.y = rep.y;
    response.width = rep.width; response.height = rep.height;
    sys_window_reply(req.owner_pid, &response);
}

static void load_default_start_menu(void) {
    const char* defaults[][2] = {
        { "Terminal",  "@builtin:terminal" },
        { "UI Studio", "@builtin:ui_studio" },
        { "Files",     "/bin/FILES.ELF" },
        { "Notes",     "/bin/NOTES.ELF" },
        { "Registry",  "/bin/REGISTRY.ELF" },
        { "Settings",  "/bin/SETTINGS.ELF" },
        { "Fonts",     "/bin/FONTS.ELF" },
    };
    for (uint32_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
        char key[32];
        const char* pfx = "USER/startmenu/";
        uint32_t k = 0;
        while (pfx[k]) { key[k] = pfx[k]; k++; }
        key[k++] = '0' + (i / 10);
        key[k++] = '0' + (i % 10);
        key[k] = '\0';
        char value[160];
        uint32_t v = 0;
        for (uint32_t j = 0; defaults[i][0][j]; j++) value[v++] = defaults[i][0][j];
        value[v++] = '|';
        for (uint32_t j = 0; defaults[i][1][j]; j++) value[v++] = defaults[i][1][j];
        value[v++] = '|';
        value[v] = '\0';
        sys_reg_set_str(key, value);
    }
}

int main(int argc, char** argv, char** envp) {
    (void)argc; (void)argv; (void)envp;

    if (render_init() != 0) return 1;
    uint32_t w = render_width(), h = render_height();
    gui_desktop_init(&g_desktop.gui, w, h);

    uint32_t bg = sys_reg_get_u32("USER/desktop/background");
    uint32_t accent = sys_reg_get_u32("USER/desktop/accent");
    if (sys_reg_get_u32("USER/desktop/transparent_drag") < 0) {
        g_desktop.gui.transparent_drag_enabled = 1;
    }
    else {
        g_desktop.gui.transparent_drag_enabled = sys_reg_get_u32("USER/desktop/transparent_drag");
    }
    gui_desktop_set_theme(&g_desktop.gui,
                          bg ? bg : COLOR_BACKGROUND,
                          accent ? accent : COLOR_ACCENT_DEFAULT);
    gui_desktop_set_launch_action(&g_desktop.gui, app_launch, &g_desktop);

    if (gui_start_menu_load(&g_desktop.gui) <= 0) {
        load_default_start_menu();
        gui_start_menu_load(&g_desktop.gui);
    }

    window_server_init(&g_desktop.server, &g_desktop.gui);
    sys_window_register();

    gui_desktop_draw(&g_desktop.gui);
    render_present();

    uint32_t last_present = sys_ticks();
    for (;;) {
        uint32_t now = sys_ticks();

        UserWindowMessage in;
        while (sys_window_recv(&in) == 0) send_window_message(&in);

        window_server_tick(&g_desktop.server, now);
        terminal_update();

        int old_x = g_desktop.gui.mouse_x;
        int old_y = g_desktop.gui.mouse_y;
        dragging_id = -1;
        for (GuiWindow* w = g_desktop.gui.windows; w; w = w->next) {
            if (w->used && w->dragging) { dragging_id = w->id; break; }
        }
        GuiWindow* dw = dragging_id >= 0 ? gui_window_find(&g_desktop.gui, dragging_id) : NULL;

        if (dw && !drag_initialized) {
            gui_desktop_draw_excluding(&g_desktop.gui, dragging_id);
            render_drag_capture();
            drag_present_x = dw->x; drag_present_y = dw->y;
            drag_width = dw->width; drag_height = dw->height;
            last_drag_present = now;
            drag_initialized = 1;
        }

        int changed = gui_desktop_poll(&g_desktop.gui);

        if (dw && now - last_drag_present >= 16) {
            GuiWindow* dw2 = gui_window_find(&g_desktop.gui, dragging_id);
            if (dw2) {
                render_drag_restore_union(drag_present_x, drag_present_y,
                                        dw2->x, dw2->y, drag_width, drag_height);
                gui_desktop_draw_window(&g_desktop.gui, dragging_id);
                render_present_union(drag_present_x, drag_present_y,
                                    dw2->x, dw2->y, drag_width, drag_height);
                drag_present_x = dw2->x; drag_present_y = dw2->y;
                last_drag_present = now;
            }
        }

        if (drag_initialized && (!gui_window_find(&g_desktop.gui, dragging_id) ||
            !gui_window_find(&g_desktop.gui, dragging_id)->dragging)) {
            gui_desktop_draw(&g_desktop.gui);
            render_present();
            g_desktop.server.pending_redraw = 0;
            last_present = now;
            drag_initialized = 0;
        }

        int mouse_moved = (g_desktop.gui.mouse_x != old_x || g_desktop.gui.mouse_y != old_y);

        if (mouse_moved && !changed && !g_desktop.server.pending_redraw &&
            !drag_initialized && dragging_id < 0) {
            render_present_cursor(old_x, old_y, g_desktop.gui.mouse_x, g_desktop.gui.mouse_y);
        }

        if (!drag_initialized && (changed > 0 || g_desktop.server.pending_redraw) &&
            now - last_present >= 16) {
            gui_desktop_draw(&g_desktop.gui);
            render_present();
            render_present_cursor(g_desktop.gui.mouse_x, g_desktop.gui.mouse_y,
                                g_desktop.gui.mouse_x, g_desktop.gui.mouse_y);
            g_desktop.server.pending_redraw = 0;
            last_present = now;
        }
    }
}