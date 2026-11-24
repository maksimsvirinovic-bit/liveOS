#include "window_server.h"
#include "gui.h"
#include <stddef.h>

static GuiWindow* owned_window(WindowServer* server, int window_id, int owner_pid) {
    if (!server || !server->desktop) return NULL;
    GuiWindow* w = gui_window_find(server->desktop, window_id);
    if (!w || w->owner_pid != owner_pid) return NULL;
    return w;
}

void window_server_init(WindowServer* server, GuiDesktop* desktop) {
    if (!server || !desktop) return;
    server->desktop = desktop;
    server->tick_ms = 0;
    server->generation = 1;
    server->pending_redraw = 0;
    server->client_count = 0;
}

void window_server_request_redraw(WindowServer* server) {
    if (server) server->pending_redraw = 1;
}

int window_server_register_client(WindowServer* server, int pid, const char* name) {
    if (!server || pid < 0 || !name) return -1;
    if (server->client_count >= 16) return -1;
    WindowClient* c = &server->clients[server->client_count++];
    c->pid = pid;
    c->generation = server->generation;
    uint32_t i = 0;
    while (name[i] && i + 1 < sizeof(c->name)) { c->name[i] = name[i]; i++; }
    c->name[i] = '\0';
    return (int)(server->client_count - 1);
}

int window_server_create_window(WindowServer* server, const char* title,
                                int x, int y, int w, int h, int owner_pid) {
    if (!server || !server->desktop || !title) return -1;
    int id = gui_window_create(server->desktop, title, x, y, w, h);
    if (id < 0) return -1;
    GuiWindow* win = gui_window_find(server->desktop, id);
    if (win) win->owner_pid = owner_pid;
    window_server_request_redraw(server);
    return id;
}

int window_server_move_window(WindowServer* server, int id, int x, int y) {
    GuiWindow* w = gui_window_find(server ? server->desktop : NULL, id);
    if (!w) return -1;
    w->x = x; w->y = y;
    window_server_request_redraw(server);
    return 0;
}

int window_server_destroy_window(WindowServer* server, int id) {
    if (!server || !server->desktop) return -1;
    gui_window_destroy(server->desktop, id);
    window_server_request_redraw(server);
    return 0;
}

int window_server_set_window_title(WindowServer* server, int id, const char* title) {
    GuiWindow* w = gui_window_find(server ? server->desktop : NULL, id);
    if (!w || !title) return -1;
    uint32_t i = 0;
    while (title[i] && i + 1 < sizeof(w->title)) { w->title[i] = title[i]; i++; }
    w->title[i] = '\0';
    window_server_request_redraw(server);
    return 0;
}

int window_server_add_group(WindowServer* server, const char* name, const int* ids, uint32_t count) {
    (void)server; (void)name; (void)ids; (void)count;
    return -1;   /* группы пока не реализованы в новом gui */
}

int window_server_add_image(WindowServer* server, const char* name, uint32_t w, uint32_t h,
                            const uint32_t* px, int alpha) {
    (void)server; (void)name; (void)w; (void)h; (void)px; (void)alpha;
    return -1;   /* images пока не реализованы в новом gui */
}

int window_server_animate_window(WindowServer* server, int id,
                                 int fx, int fy, int tx, int ty,
                                 uint32_t dur, GuiAnimationKind kind) {
    if (!server || !server->desktop) return -1;
    GuiWindow* w = gui_window_find(server->desktop, id);
    if (!w) return -1;
    if (gui_desktop_add_animation(server->desktop, id, fx, fy, tx, ty,
                                  server->tick_ms ? server->tick_ms : 0, dur, kind) < 0) return -1;
    window_server_request_redraw(server);
    return 0;
}

/* ---- dispatch ---- */

static void copy_payload_to_text(const WindowServerMessage* req, char* out, uint32_t cap) {
    if (!out || cap == 0) return;
    uint32_t n = 0;
    if (req->payload_size > 0 && req->payload_size < cap) {
        for (uint32_t i = 0; i < req->payload_size; i++) out[n++] = (char)req->payload[i];
    }
    out[n] = '\0';
}

int window_server_dispatch_message(WindowServer* server,
                                   const WindowServerMessage* req,
                                   WindowServerMessage* rep) {
    if (!server || !req || !rep || !server->desktop) return -1;
    *rep = (WindowServerMessage){0};
    rep->type = req->type;
    rep->window_id = -1;
    rep->result = -1;

    char text[256];
    copy_payload_to_text(req, text, sizeof(text));

    switch (req->type) {
        case WINDOW_MSG_CREATE: {
            const char* title = text[0] ? text : "Window";
            rep->window_id = window_server_create_window(server, title,
                req->x, req->y, req->width, req->height, req->owner_pid);
            rep->result = rep->window_id >= 0 ? 0 : -1;
            break;
        }
        case WINDOW_MSG_MOVE:
            if (owned_window(server, req->window_id, req->owner_pid))
                rep->result = window_server_move_window(server, req->window_id, req->x, req->y);
            break;
        case WINDOW_MSG_DESTROY:
            if (owned_window(server, req->window_id, req->owner_pid))
                rep->result = window_server_destroy_window(server, req->window_id);
            break;
        case WINDOW_MSG_SET_TITLE:
            if (owned_window(server, req->window_id, req->owner_pid))
                rep->result = window_server_set_window_title(server, req->window_id,
                                                             text[0] ? text : "Window");
            break;
        case WINDOW_MSG_CREATE_CONTROL: {
            GuiWindow* w = gui_window_find(server->desktop, req->window_id);
            if (!w) break;
            int cid = gui_control_add(server->desktop, req->window_id,
                (GuiControlType)req->control_type,
                req->x, req->y, req->width, req->height, text);
            if (cid >= 0) {
                rep->control_id = cid;
                rep->result = 0;
                window_server_request_redraw(server);
            }
            break;
        }
        case WINDOW_MSG_UPDATE_CONTROL: {
            GuiWindow* w = gui_window_find(server->desktop, req->window_id);
            if (!w) break;
            GuiControl* c = gui_control_find(server->desktop, req->window_id, req->control_id);
            if (!c) break;
            if (c->type == GUI_LIST) {
                int idx = req->selected;
                if (idx < 0) idx = req->x;
                if (idx >= 0) {
                    gui_list_set_item(server->desktop, req->window_id, req->control_id,
                                      (uint32_t)idx, text);
                    if (req->selected >= 0) c->selected = req->selected;
                }
            } else if (req->payload_size) {
                gui_control_set_text(server->desktop, req->window_id, req->control_id, text);
            }
            rep->result = 0;
            window_server_request_redraw(server);
            break;
        }
        case WINDOW_MSG_REDRAW:
            window_server_request_redraw(server);
            rep->result = 0;
            break;
        case WINDOW_MSG_ANIMATE:
            rep->result = window_server_animate_window(server, req->window_id,
                req->x, req->y, req->width, req->height, req->duration_ms,
                req->has_alpha ? GUI_ANIM_FADE : GUI_ANIM_MOVE);
            break;
        default:
            break;
    }
    return rep->result;
}

int window_server_process_messages(WindowServer* server,
                                   const WindowServerMessage* requests,
                                   uint32_t count, WindowServerMessage* replies) {
    if (!server || !requests || !replies || count == 0) return -1;
    for (uint32_t i = 0; i < count; i++) {
        replies[i] = (WindowServerMessage){0};
        window_server_dispatch_message(server, &requests[i], &replies[i]);
    }
    return 0;
}

void window_server_tick(WindowServer* server, uint32_t now_ms) {
    if (!server || !server->desktop) return;
    server->tick_ms = now_ms;
    gui_desktop_update_animations(server->desktop, now_ms);
}