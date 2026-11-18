#ifndef LIVEOS_DESKTOP_WINDOW_SERVER_H
#define LIVEOS_DESKTOP_WINDOW_SERVER_H

#include <stdint.h>
#include "gui.h"

typedef struct {
    int pid;
    char name[32];
    uint32_t generation;
} WindowClient;

typedef enum {
    WINDOW_MSG_NONE = 0,
    WINDOW_MSG_CREATE,
    WINDOW_MSG_MOVE,
    WINDOW_MSG_DESTROY,
    WINDOW_MSG_SET_TITLE,
    WINDOW_MSG_GROUP,
    WINDOW_MSG_IMAGE,
    WINDOW_MSG_ANIMATE,
    WINDOW_MSG_REDRAW,
    WINDOW_MSG_CREATE_CONTROL,
    WINDOW_MSG_UPDATE_CONTROL
} WindowServerMessageType;

typedef struct {
    WindowServerMessageType type;
    int32_t window_id;
    int32_t owner_pid;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t control_type;
    int32_t control_id;
    int32_t selected;
    int32_t input_type;
    int32_t input_key;
    int32_t result;
    int32_t has_alpha;
    uint32_t start_ms;
    uint32_t duration_ms;
    uint32_t payload_size;
    uint8_t payload[256];
    char title[48];
    char name[32];
} WindowServerMessage;

typedef struct {
    GuiDesktop* desktop;
    uint32_t tick_ms;
    uint32_t generation;
    int pending_redraw;
    uint32_t lock;
    WindowClient clients[16];
    uint32_t client_count;
} WindowServer;

void window_server_init(WindowServer* server, GuiDesktop* desktop);
void window_server_request_redraw(WindowServer* server);
void window_server_tick(WindowServer* server, uint32_t now_ms);
int window_server_register_client(WindowServer* server, int pid, const char* name);
int window_server_create_window(WindowServer* server, const char* title, int x, int y,
                                int width, int height, int owner_pid);
int window_server_move_window(WindowServer* server, int window_id, int x, int y);
int window_server_destroy_window(WindowServer* server, int window_id);
int window_server_set_window_title(WindowServer* server, int window_id, const char* title);
int window_server_add_group(WindowServer* server, const char* name, const int* ids, uint32_t count);
int window_server_add_image(WindowServer* server, const char* name, uint32_t width, uint32_t height,
                           const uint32_t* pixels, int has_alpha);
int window_server_animate_window(WindowServer* server, int window_id, int from_x, int from_y,
                                int to_x, int to_y, uint32_t duration_ms, GuiAnimationKind kind);
int window_server_dispatch_message(WindowServer* server, const WindowServerMessage* request,
                                  WindowServerMessage* reply);
int window_server_process_messages(WindowServer* server, const WindowServerMessage* requests,
                                  uint32_t count, WindowServerMessage* replies);

#endif
