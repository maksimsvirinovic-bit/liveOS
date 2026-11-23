#ifndef LIVEOS_DESKTOP_GUI_H
#define LIVEOS_DESKTOP_GUI_H

#include <stdint.h>

#define GUI_MAX_WINDOWS 10
#define GUI_MAX_CONTROLS 16
#define GUI_MAX_GROUPS 8
#define GUI_MAX_ANIMATIONS 16
#define GUI_MAX_IMAGES 16
#define GUI_TEXT_MAX 1024
#define GUI_WINDOW_TITLE 48
#define GUI_LIST_ITEMS 8
#define GUI_LIST_CAPACITY 256
#define GUI_LIST_TEXT_MAX 96
#define GUI_CONTEXT_ITEMS 8

typedef enum {
    GUI_LABEL,
    GUI_BUTTON,
    GUI_TEXTFIELD,
    GUI_PANEL,
    GUI_LIST,
    GUI_TEXTAREA,
    GUI_CANVAS
} GuiControlType;

typedef void (*GuiCanvasAction)(void* context, int x, int y, int width, int height);

typedef struct {
    GuiControlType type;
    int x;
    int y;
    int width;
    int height;
    int focused;
    int selected;
    int scroll;
    int cursor;
    uint32_t item_count;
    char text[GUI_TEXT_MAX];
    char items[GUI_LIST_CAPACITY][GUI_LIST_TEXT_MAX];
    GuiCanvasAction canvas_action;
    void* canvas_context;
} GuiControl;

typedef void (*GuiAction)(void* context, GuiControl* control);
typedef void (*GuiLaunchAction)(int application);
typedef void (*GuiListAction)(void* context, GuiControl* control);
typedef void (*GuiFocusAction)(void* context, int focused);
typedef void (*GuiCloseAction)(void* context);
typedef void (*GuiContextAction)(void* context, int x, int y);
typedef void (*GuiMenuAction)(void* context, int item);
typedef void (*GuiKeyAction)(void* context, int key);

typedef enum {
    GUI_ANIM_NONE = 0,
    GUI_ANIM_MOVE,
    GUI_ANIM_FADE,
    GUI_ANIM_PULSE
} GuiAnimationKind;

typedef struct {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    uint8_t has_alpha;
    char name[32];
    uint32_t* pixels;
} GuiImage;

typedef struct {
    uint32_t id;
    int window_id;
    int from_x;
    int from_y;
    int to_x;
    int to_y;
    uint32_t start_ms;
    uint32_t duration_ms;
    GuiAnimationKind kind;
    int active;
} GuiAnimation;

typedef struct {
    char name[32];
    int ids[GUI_MAX_WINDOWS];
    uint32_t count;
    uint32_t flags;
} GuiWindowGroup;

typedef struct {
    uint32_t lock;
    uint32_t owner_cpu;
    uint32_t generation;
} GuiSmpLock;

typedef struct {
    int used;
    int visible;
    int modal;
    int dragging;
    int minimized;
    int maximized;
    int drag_dx;
    int drag_dy;
    int normal_x;
    int normal_y;
    int normal_width;
    int normal_height;
    int x;
    int y;
    int width;
    int height;
    int z;
    int focused_control;
    int owner_pid;
    int remote_id;
    GuiAction action;
    void* action_context;
    GuiListAction list_action;
    void* list_context;
    GuiListAction list_activate_action;
    void* list_activate_context;
    GuiFocusAction focus_action;
    void* focus_context;
    GuiCloseAction close_action;
    void* close_context;
    GuiContextAction context_action;
    void* context_context;
    char title[GUI_WINDOW_TITLE];
    GuiControl controls[GUI_MAX_CONTROLS];
    uint32_t control_count;
} GuiWindow;

typedef struct GuiWindowNode {
    GuiWindow w;
    int id;                                // стабильный id (не индекс)
    struct GuiWindowNode* next;
} GuiWindowNode;

typedef struct {
    uint32_t width;
    uint32_t height;
    int mouse_x;
    int mouse_y;
    uint32_t mouse_buttons;
    uint32_t old_buttons;
    int focused_window;
    int next_z;
    int start_open;
    int context_open;
    int context_x;
    int context_y;
    int context_window;
    uint32_t context_count;
    char context_items[GUI_CONTEXT_ITEMS][64];
    GuiMenuAction context_action;
    void* context_context;
    GuiLaunchAction launch_action;
    GuiKeyAction key_action;
    void* key_context;
    uint32_t background_color;
    uint32_t accent_color;
    GuiSmpLock render_lock;
    GuiWindowGroup groups[GUI_MAX_GROUPS];
    uint32_t group_count;
    GuiImage images[GUI_MAX_IMAGES];
    uint32_t image_count;
    int remote_mode;
    GuiAnimation animations[GUI_MAX_ANIMATIONS];
    uint32_t animation_count;
    GuiWindowNode* windows;
    uint32_t window_count;
    int next_window_id;
} GuiDesktop;

typedef struct {
    GuiDesktop desktop;
    void* framebuffer;
    uint32_t width;
    uint32_t height;
    int remote_window_id;
} GuiAppContext;

int gui_app_init(GuiAppContext* app);
void gui_app_draw(GuiAppContext* app);
int gui_app_create_window(GuiAppContext* app, const char* title,
                          int x, int y, int width, int height);
int gui_app_add_control(GuiAppContext* app, int window_id, GuiControlType type,
                        int x, int y, int width, int height, const char* text);
void gui_app_set_action(GuiAppContext* app, int window_id, GuiAction action, void* context);
void gui_app_set_list_action(GuiAppContext* app, int window_id, GuiListAction action, void* context);
void gui_app_set_list_activate_action(GuiAppContext* app, int window_id,
                                      GuiListAction action, void* context);
void gui_app_set_close_action(GuiAppContext* app, int window_id,
                              GuiCloseAction action, void* context);
int gui_app_poll(GuiAppContext* app);

void gui_desktop_init(GuiDesktop* desktop, uint32_t width, uint32_t height);
int gui_window_create(GuiDesktop* desktop, const char* title, int x, int y,
                      int width, int height, GuiAction action, void* context);
void gui_window_destroy(GuiDesktop* desktop, int window_id);
int gui_control_add(GuiDesktop* desktop, int window_id, GuiControlType type,
                    int x, int y, int width, int height, const char* text);
int gui_canvas_set_action(GuiDesktop* desktop, int window_id, int control_id,
                          GuiCanvasAction action, void* context);
int gui_list_set_item(GuiDesktop* desktop, int window_id, int control_id,
                      uint32_t item, const char* text);
int gui_list_get_selected(const GuiDesktop* desktop, int window_id, int control_id);
void gui_desktop_draw(GuiDesktop* desktop);
void gui_desktop_set_launch_action(GuiDesktop* desktop, GuiLaunchAction action);
int gui_window_group_add(GuiDesktop* desktop, const char* name, const int* ids, uint32_t count);
int gui_window_group_remove(GuiDesktop* desktop, const char* name);
int gui_image_load_from_memory(GuiDesktop* desktop, const char* name, uint32_t width, uint32_t height,
                              const uint32_t* pixels, int has_alpha);
int gui_image_load_from_file(GuiDesktop* desktop, const char* name, const char* path);
int gui_desktop_add_animation(GuiDesktop* desktop, int window_id, int from_x, int from_y,
                             int to_x, int to_y, uint32_t start_ms, uint32_t duration_ms,
                             GuiAnimationKind kind);
void gui_desktop_update_animations(GuiDesktop* desktop, uint32_t now_ms);
void gui_window_set_list_action(GuiDesktop* desktop, int window_id,
                                GuiListAction action, void* context);
void gui_window_set_list_activate_action(GuiDesktop* desktop, int window_id,
                                         GuiListAction action, void* context);
void gui_window_set_focus_action(GuiDesktop* desktop, int window_id,
                                 GuiFocusAction action, void* context);
void gui_window_set_close_action(GuiDesktop* desktop, int window_id,
                                 GuiCloseAction action, void* context);
void gui_window_set_context_action(GuiDesktop* desktop, int window_id,
                                   GuiContextAction action, void* context);
void gui_context_menu_show(GuiDesktop* desktop, int x, int y,
                           const char* const* items, uint32_t count,
                           int window_id, GuiMenuAction action, void* context);
void gui_desktop_set_theme(GuiDesktop* desktop, uint32_t background, uint32_t accent);
void gui_desktop_set_key_action(GuiDesktop* desktop, GuiKeyAction action, void* context);
void gui_desktop_handle_key(GuiDesktop* desktop, int key);
void gui_desktop_handle_pointer(GuiDesktop* desktop, int x, int y, uint32_t buttons);
int gui_desktop_poll(GuiDesktop* desktop);
void gui_desktop_draw_excluding(GuiDesktop* desktop, int excluded_window);
void gui_desktop_draw_window(GuiDesktop* desktop, int window_id);
GuiWindow* gui_window_find(GuiDesktop* desktop, int window_id);
int gui_window_id(const GuiDesktop* desktop, const GuiWindow* w);


#endif
