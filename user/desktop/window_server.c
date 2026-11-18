#include "window_server.h"
#include <stddef.h>
#include "../ubase.h"

static int gui_window_group_find(GuiDesktop* desktop, const char* name) {
    if (!desktop || !name) return -1;
    for (uint32_t i = 0; i < desktop->group_count; i++) {
        if (desktop->groups[i].name[0] && name[0] == desktop->groups[i].name[0] &&
            desktop->groups[i].name[0] == name[0]) {
            uint32_t j = 0;
            while (name[j] && desktop->groups[i].name[j] && name[j] == desktop->groups[i].name[j]) j++;
            if (!name[j] && !desktop->groups[i].name[j]) return (int)i;
        }
    }
    return -1;
}

static void gui_smp_lock(GuiSmpLock* lock, uint32_t cpu_id) {
    if (!lock) return;
    lock->owner_cpu = cpu_id;
    lock->generation++;
    lock->lock = 1;
}

static void gui_smp_unlock(GuiSmpLock* lock) {
    if (!lock) return;
    lock->lock = 0;
    lock->owner_cpu = 0;
}

static GuiWindow* owned_window(WindowServer* server, int window_id, int owner_pid) {
    if (!server || !server->desktop || window_id < 0 || window_id >= GUI_MAX_WINDOWS) return NULL;
    GuiWindow* window = &server->desktop->windows[window_id];
    if (!window->used || window->owner_pid != owner_pid) return NULL;
    return window;
}

void window_server_init(WindowServer* server, GuiDesktop* desktop) {
    if (!server || !desktop) return;
    server->desktop = desktop;
    server->tick_ms = 0;
    server->generation = 1;
    server->pending_redraw = 0;
    server->lock = 0;
    server->client_count = 0;
    gui_smp_lock(&desktop->render_lock, 0);
}

void window_server_request_redraw(WindowServer* server) {
    if (!server || !server->desktop) return;
    server->pending_redraw = 1;
}

int window_server_register_client(WindowServer* server, int pid, const char* name) {
    if (!server || pid < 0 || !name) return -1;
    if (server->client_count >= 16) return -1;
    WindowClient* client = &server->clients[server->client_count++];
    client->pid = pid;
    client->generation = server->generation;
    uint32_t i = 0;
    while (name[i] && i + 1 < sizeof(client->name)) {
        client->name[i] = name[i];
        i++;
    }
    client->name[i] = '\0';
    return (int)(server->client_count - 1);
}

int window_server_create_window(WindowServer* server, const char* title, int x, int y,
                                int width, int height, int owner_pid) {
    if (!server || !server->desktop || !title) return -1;
    int id = gui_window_create(server->desktop, title, x, y, width, height, NULL, NULL);
    if (id < 0) return -1;
    server->desktop->windows[id].owner_pid = owner_pid;
    window_server_request_redraw(server);
    return id;
}

int window_server_move_window(WindowServer* server, int window_id, int x, int y) {
    if (!server || !server->desktop || window_id < 0 || window_id >= GUI_MAX_WINDOWS) return -1;
    GuiWindow* window = &server->desktop->windows[window_id];
    if (!window->used) return -1;
    window->x = x;
    window->y = y;
    window_server_request_redraw(server);
    return 0;
}

int window_server_destroy_window(WindowServer* server, int window_id) {
    if (!server || !server->desktop || window_id < 0 || window_id >= GUI_MAX_WINDOWS) return -1;
    gui_window_destroy(server->desktop, window_id);
    window_server_request_redraw(server);
    return 0;
}

int window_server_set_window_title(WindowServer* server, int window_id, const char* title) {
    if (!server || !server->desktop || !title || window_id < 0 || window_id >= GUI_MAX_WINDOWS) return -1;
    GuiWindow* window = &server->desktop->windows[window_id];
    if (!window->used) return -1;
    uint32_t i = 0;
    while (title[i] && i + 1 < sizeof(window->title)) {
        window->title[i] = title[i];
        i++;
    }
    window->title[i] = '\0';
    window_server_request_redraw(server);
    return 0;
}

int window_server_add_group(WindowServer* server, const char* name, const int* ids, uint32_t count) {
    if (!server || !server->desktop) return -1;
    return gui_window_group_add(server->desktop, name, ids, count);
}

int window_server_add_image(WindowServer* server, const char* name, uint32_t width, uint32_t height,
                           const uint32_t* pixels, int has_alpha) {
    if (!server || !server->desktop) return -1;
    return gui_image_load_from_memory(server->desktop, name, width, height, pixels, has_alpha);
}

int window_server_animate_window(WindowServer* server, int window_id, int from_x, int from_y,
                                int to_x, int to_y, uint32_t duration_ms, GuiAnimationKind kind) {
    if (!server || !server->desktop || window_id < 0 || window_id >= GUI_MAX_WINDOWS) return -1;
    if (gui_desktop_add_animation(server->desktop, window_id, from_x, from_y, to_x, to_y,
                                  server->tick_ms ? server->tick_ms : 0, duration_ms, kind) < 0) {
        return -1;
    }
    window_server_request_redraw(server);
    return 0;
}

int window_server_dispatch_message(WindowServer* server, const WindowServerMessage* request,
                                  WindowServerMessage* reply) {
    if (!server || !request || !reply) return -1;
    reply->type = request->type;
    reply->window_id = -1;
    reply->result = -1;
    char title_buf[256];
    if (request->payload_size > 0 && request->payload_size < sizeof(title_buf)) {
        for (uint32_t i = 0; i < request->payload_size; i++) title_buf[i] = (char)request->payload[i];
        title_buf[request->payload_size] = '\0';
    } else {
        title_buf[0] = '\0';
    }
    if (title_buf[0] != '\0') {
        uint32_t n = 0;
        while (title_buf[n] && n + 1 < sizeof(reply->title)) {
            reply->title[n] = title_buf[n];
            n++;
        }
        reply->title[n] = '\0';
    }
    switch (request->type) {
        case WINDOW_MSG_CREATE: {
            const char* title = title_buf[0] ? title_buf : "Window";
            reply->window_id = window_server_create_window(server, title,
                request->x, request->y, request->width, request->height, request->owner_pid);
            reply->result = reply->window_id >= 0 ? 0 : -1;
            break;
        }
        case WINDOW_MSG_MOVE:
            reply->result = owned_window(server, request->window_id, request->owner_pid)
                ? window_server_move_window(server, request->window_id, request->x, request->y) : -1;
            break;
        case WINDOW_MSG_DESTROY:
            if (owned_window(server, request->window_id, request->owner_pid)) {
                reply->result = window_server_destroy_window(server, request->window_id);
            }
            break;
        case WINDOW_MSG_SET_TITLE:
            reply->result = owned_window(server, request->window_id, request->owner_pid)
                ? window_server_set_window_title(server, request->window_id, title_buf[0] ? title_buf : "Window") : -1;
            break;
        case WINDOW_MSG_CREATE_CONTROL: {
            GuiWindow* window = NULL;
            if (request->window_id >= 0 && request->window_id < GUI_MAX_WINDOWS) {
                window = &server->desktop->windows[request->window_id];
                if (!window->used) window = NULL;
            }
            int control_id = window ? gui_control_add(server->desktop, request->window_id,
                (GuiControlType)request->control_type, request->x, request->y,
                request->width, request->height, title_buf) : -1;
            if (control_id >= 0) {
                reply->control_id = control_id;
                reply->result = 0;
                window_server_request_redraw(server);
            }
            break;
        }
        case WINDOW_MSG_UPDATE_CONTROL: {
            GuiWindow* window = NULL;
            if (request->window_id >= 0 && request->window_id < GUI_MAX_WINDOWS) {
                window = &server->desktop->windows[request->window_id];
                if (!window->used) window = NULL;
            }
            if (window && request->control_id >= 0 &&
                (uint32_t)request->control_id < window->control_count) {
                GuiControl* control = &window->controls[request->control_id];
                if (control->type == GUI_LIST) {
                    int item_index = request->selected;
                    if (item_index < 0) item_index = request->x;
                    if (item_index >= 0 && item_index < GUI_LIST_CAPACITY) {
                        uint32_t i = 0;
                        while (title_buf[i] && i + 1 < sizeof(control->items[item_index])) {
                            control->items[item_index][i] = title_buf[i];
                            i++;
                        }
                        control->items[item_index][i] = '\0';
                        if (title_buf[0] == '\0') control->items[item_index][0] = '\0';
                        if ((uint32_t)item_index >= control->item_count) {
                            control->item_count = (uint32_t)item_index + 1;
                        }
                    }
                    if (request->selected >= 0) control->selected = request->selected;
                } else if (request->payload_size) {
                    uint32_t i = 0;
                    while (title_buf[i] && i + 1 < sizeof(control->text)) {
                        control->text[i] = title_buf[i];
                        i++;
                    }
                    control->text[i] = '\0';
                }
                reply->result = 0;
                window_server_request_redraw(server);
            }
            break;
        }
        case WINDOW_MSG_REDRAW:
            window_server_request_redraw(server);
            reply->result = 0;
            break;
        case WINDOW_MSG_ANIMATE:
            reply->result = window_server_animate_window(server, request->window_id,
                request->x, request->y, request->width, request->height, request->duration_ms, request->has_alpha ? GUI_ANIM_FADE : GUI_ANIM_MOVE);
            break;
        default:
            reply->result = -1;
            break;
    }
    return reply->result;
}

int window_server_process_messages(WindowServer* server, const WindowServerMessage* requests,
                                  uint32_t count, WindowServerMessage* replies) {
    if (!server || !requests || !replies || count == 0) return -1;
    for (uint32_t i = 0; i < count; i++) {
        replies[i] = (WindowServerMessage){0};
        if (window_server_dispatch_message(server, &requests[i], &replies[i]) < 0) {
            replies[i].result = -1;
            replies[i].type = requests[i].type;
        }
    }
    return 0;
}

void window_server_tick(WindowServer* server, uint32_t now_ms) {
    if (!server || !server->desktop) return;
    server->tick_ms = now_ms;
    for (uint32_t i = 0; i < server->desktop->animation_count; i++) {
        GuiAnimation* anim = &server->desktop->animations[i];
        if (!anim->active) continue;
        uint32_t elapsed = now_ms - anim->start_ms;
        if (elapsed >= anim->duration_ms) {
            anim->active = 0;
            server->desktop->windows[anim->window_id].x = anim->to_x;
            server->desktop->windows[anim->window_id].y = anim->to_y;
            server->pending_redraw = 1;
            continue;
        }
        int blend = (int)((elapsed * 1000) / (int)anim->duration_ms);
        int x = anim->from_x + ((anim->to_x - anim->from_x) * blend) / 1000;
        int y = anim->from_y + ((anim->to_y - anim->from_y) * blend) / 1000;
        if (anim->window_id >= 0 && anim->window_id < GUI_MAX_WINDOWS) {
            server->desktop->windows[anim->window_id].x = x;
            server->desktop->windows[anim->window_id].y = y;
        }
        server->pending_redraw = 1;
    }

    gui_smp_unlock(&server->desktop->render_lock);
}

int gui_window_group_add(GuiDesktop* desktop, const char* name, const int* ids, uint32_t count) {
    if (!desktop || !name || !ids || count == 0 || count >= GUI_MAX_WINDOWS) return -1;
    int existing = gui_window_group_find(desktop, name);
    if (existing >= 0) return -1;
    if (desktop->group_count >= GUI_MAX_GROUPS) return -1;
    GuiWindowGroup* group = &desktop->groups[desktop->group_count++];
    uint32_t n = 0;
    while (name[n] && n + 1 < sizeof(group->name)) { group->name[n] = name[n]; n++; }
    group->name[n] = '\0';
    group->count = count;
    for (uint32_t i = 0; i < count; i++) group->ids[i] = ids[i];
    group->flags = 0;
    return 0;
}

int gui_window_group_remove(GuiDesktop* desktop, const char* name) {
    if (!desktop || !name) return -1;
    int idx = gui_window_group_find(desktop, name);
    if (idx < 0) return -1;
    for (uint32_t i = idx; i + 1 < desktop->group_count; i++) {
        desktop->groups[i] = desktop->groups[i + 1];
    }
    desktop->group_count--;
    return 0;
}

int gui_image_load_from_memory(GuiDesktop* desktop, const char* name, uint32_t width, uint32_t height,
                               const uint32_t* pixels, int has_alpha) {
    if (!desktop || !name || !pixels || width == 0 || height == 0 || desktop->image_count >= GUI_MAX_IMAGES) return -1;
    GuiImage* image = &desktop->images[desktop->image_count++];
    image->id = desktop->image_count;
    image->width = width;
    image->height = height;
    image->has_alpha = has_alpha ? 1 : 0;
    uint32_t n = 0; while (name[n] && n + 1 < sizeof(image->name)) { image->name[n] = name[n]; n++; }
    image->name[n] = '\0';
    image->pixels = (uint32_t*)pixels;
    return (int)image->id;
}

int gui_image_load_from_file(GuiDesktop* desktop, const char* name, const char* path) {
    (void)desktop; (void)name; (void)path;
    return -1;
}

int gui_desktop_add_animation(GuiDesktop* desktop, int window_id, int from_x, int from_y,
                             int to_x, int to_y, uint32_t start_ms, uint32_t duration_ms,
                             GuiAnimationKind kind) {
    if (!desktop || window_id < 0 || window_id >= GUI_MAX_WINDOWS || desktop->animation_count >= GUI_MAX_ANIMATIONS) return -1;
    GuiAnimation* anim = &desktop->animations[desktop->animation_count++];
    anim->id = desktop->animation_count;
    anim->window_id = window_id;
    anim->from_x = from_x;
    anim->from_y = from_y;
    anim->to_x = to_x;
    anim->to_y = to_y;
    anim->start_ms = start_ms;
    anim->duration_ms = duration_ms;
    anim->kind = kind;
    anim->active = 1;
    return (int)anim->id;
}

void gui_desktop_update_animations(GuiDesktop* desktop, uint32_t now_ms) {
    if (!desktop) return;
    for (uint32_t i = 0; i < desktop->animation_count; i++) {
        GuiAnimation* anim = &desktop->animations[i];
        if (!anim->active) continue;
        uint32_t elapsed = now_ms - anim->start_ms;
        if (elapsed >= anim->duration_ms) {
            anim->active = 0;
            desktop->windows[anim->window_id].x = anim->to_x;
            desktop->windows[anim->window_id].y = anim->to_y;
            continue;
        }
        int progress = (int)((elapsed * 1000u) / anim->duration_ms);
        desktop->windows[anim->window_id].x = anim->from_x + ((anim->to_x - anim->from_x) * progress) / 1000;
        desktop->windows[anim->window_id].y = anim->from_y + ((anim->to_y - anim->from_y) * progress) / 1000;
    }
}
