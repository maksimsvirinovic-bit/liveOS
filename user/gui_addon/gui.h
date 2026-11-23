#ifndef LIVEOS_DESKTOP_GUI_H
#define LIVEOS_DESKTOP_GUI_H

#include <stdint.h>

#define GUI_WINDOW_TITLE 48
#define GUI_TEXT_MAX     1024

typedef enum {
    GUI_LABEL,
    GUI_BUTTON,
    GUI_TEXTFIELD,
    GUI_PANEL,
    GUI_LIST,
    GUI_TEXTAREA,
    GUI_CANVAS
} GuiControlType;

typedef struct GuiControl GuiControl;

typedef void (*GuiCanvasAction)(void* context, int x, int y, int width, int height);
typedef void (*GuiControlAction)(void* context, GuiControl* control);

typedef enum {
    GUI_ANIM_NONE = 0,
    GUI_ANIM_MOVE,
    GUI_ANIM_FADE,
    GUI_ANIM_PULSE
} GuiAnimationKind;

struct GuiControl {
    GuiControlType type;
    int id;
    int x, y, width, height;
    int focused, selected, scroll, cursor;
    int visible, disabled;

    uint32_t fg_color, bg_color, border_color;
    uint32_t hover_color, pressed_color;
    uint8_t font_scale;

    GuiControlAction action;
    void* action_context;
    GuiCanvasAction canvas_action;
    void* canvas_context;

    char* text;
    uint32_t text_capacity;

    char** items;
    uint32_t item_count;
    uint32_t item_capacity;
    uint32_t item_text_max;

    struct GuiControl* next;
};

typedef void (*GuiAction)(void* context, GuiControl* control);
typedef void (*GuiListAction)(void* context, GuiControl* control);
typedef void (*GuiFocusAction)(void* context, int focused);
typedef void (*GuiCloseAction)(void* context);
typedef void (*GuiContextAction)(void* context, int x, int y);
typedef void (*GuiMenuAction)(void* context, int item);
typedef void (*GuiKeyAction)(void* context, int key);
typedef void (*GuiLaunchAction)(void* context, const char* action, const char* args);

typedef struct GuiWindow {
    int id;
    int used, visible, modal, dragging, minimized, maximized;
    int drag_dx, drag_dy;
    int normal_x, normal_y, normal_width, normal_height;
    int x, y, width, height, z;
    int focused_control;
    int owner_pid, remote_id;

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

    GuiControl* controls;
    uint32_t control_count;
    int next_control_id;

    struct GuiWindow* next;
} GuiWindow;

typedef struct {
    uint32_t id;
    int window_id;
    int from_x, from_y, to_x, to_y;
    uint32_t start_ms, duration_ms;
    GuiAnimationKind kind;
    int active;
} GuiAnimation;

typedef struct {
    char* label;
    char* action;
    char* args;
} GuiMenuItem;

typedef struct {
    uint32_t width, height;
    int mouse_x, mouse_y;
    uint32_t mouse_buttons, old_buttons;
    int focused_window;
    int next_z, next_window_id;
    int start_open, start_scroll;

    GuiMenuItem* menu_items;
    uint32_t menu_item_count;
    uint32_t menu_item_capacity;

    int context_open, context_x, context_y, context_window;
    char** context_items;
    uint32_t context_count;
    uint32_t context_capacity;
    GuiMenuAction context_action;
    void* context_context;

    GuiLaunchAction launch_action;
    void* launch_context;
    GuiKeyAction key_action;
    void* key_context;

    uint32_t background_color, accent_color;

    GuiWindow* windows;
    uint32_t window_count;

    GuiAnimation* animations;
    uint32_t animation_count;
    uint32_t animation_capacity;
} GuiDesktop;

void  gui_desktop_init(GuiDesktop* d, uint32_t w, uint32_t h);
void  gui_desktop_destroy(GuiDesktop* d);
void  gui_desktop_set_theme(GuiDesktop* d, uint32_t bg, uint32_t accent);
void  gui_desktop_set_launch_action(GuiDesktop* d, GuiLaunchAction a, void* ctx);
void  gui_desktop_set_key_action(GuiDesktop* d, GuiKeyAction a, void* ctx);
void  gui_desktop_draw(GuiDesktop* d);
int   gui_desktop_poll(GuiDesktop* d);
GuiWindow* gui_window_find(GuiDesktop* d, int id);
int        gui_window_create(GuiDesktop* d, const char* title, int x, int y,
                             int w, int h);
void       gui_window_destroy(GuiDesktop* d, int id);
void       gui_window_set_close_action(GuiDesktop* d, int id, GuiCloseAction a, void* ctx);
void       gui_window_set_focus_action(GuiDesktop* d, int id, GuiFocusAction a, void* ctx);
void       gui_window_set_context_action(GuiDesktop* d, int id, GuiContextAction a, void* ctx);
void       gui_window_set_list_action(GuiDesktop* d, int id, GuiListAction a, void* ctx);
void       gui_window_set_list_activate_action(GuiDesktop* d, int id, GuiListAction a, void* ctx);
GuiControl* gui_control_find(GuiDesktop* d, int window_id, int control_id);
int         gui_control_add(GuiDesktop* d, int window_id, GuiControlType type,
                            int x, int y, int w, int h, const char* text);
void        gui_control_destroy(GuiDesktop* d, int window_id, int control_id);
int         gui_control_set_text(GuiDesktop* d, int window_id, int control_id,
                                 const char* text);
const char* gui_control_get_text(GuiDesktop* d, int window_id, int control_id);
void        gui_control_set_action(GuiDesktop* d, int window_id, int control_id,
                                   GuiControlAction a, void* ctx);
void        gui_control_set_canvas_action(GuiDesktop* d, int window_id, int control_id,
                                          GuiCanvasAction a, void* ctx);
void        gui_control_set_colors(GuiDesktop* d, int window_id, int control_id,
                                   uint32_t fg, uint32_t bg, uint32_t border);
int  gui_list_add_item(GuiDesktop* d, int window_id, int control_id, const char* text);
int  gui_list_set_item(GuiDesktop* d, int window_id, int control_id,
                       uint32_t index, const char* text);
void gui_list_clear(GuiDesktop* d, int window_id, int control_id);
int  gui_list_get_selected(GuiDesktop* d, int window_id, int control_id);
void gui_context_menu_show(GuiDesktop* d, int x, int y,
                           const char* const* items, uint32_t count,
                           int window_id, GuiMenuAction action, void* context);
void gui_context_menu_hide(GuiDesktop* d);
int  gui_start_menu_load(GuiDesktop* d);
void gui_start_menu_clear(GuiDesktop* d);
int  gui_desktop_add_animation(GuiDesktop* d, int window_id,
                               int from_x, int from_y, int to_x, int to_y,
                               uint32_t start_ms, uint32_t duration_ms,
                               GuiAnimationKind kind);
void gui_desktop_update_animations(GuiDesktop* d, uint32_t now_ms);

#endif