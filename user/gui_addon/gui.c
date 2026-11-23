#include "gui.h"
#include "../sys.h"
#include "render.h"
#include <stddef.h>

#define KBD_KEY_UP      (-10)
#define KBD_KEY_DOWN    (-11)
#define KBD_KEY_LEFT    (-12)
#define KBD_KEY_RIGHT   (-13)
#define KBD_KEY_DELETE  (-18)

#define C_BG       0x00121922u
#define C_DESK     0x001B2835u
#define C_PANEL    0x00232F3Du
#define C_TITLE    0x002A3949u
#define C_TITLE_F  0x0000A7C4u
#define C_BORDER   0x00485B6Eu
#define C_TEXT     0x00E8F0F5u
#define C_MUTED    0x0096A8B5u
#define C_BUTTON   0x00364658u
#define C_BUTTON_H 0x00465D72u
#define C_FOCUS    0x0000D8FFu
#define C_WHITE    0x00FFFFFFu
#define C_BLACK    0x00000000u
#define C_CLOSE    0x00B84A55u
#define C_MINIMIZE 0x00465D72u
#define C_MAXIMIZE 0x00465D72u
#define TASKBAR_H  28
#define TITLE_H    24
#define ITEM_COUNT 8
#define TITLE_BUTTON 18
#define MENU_HEIGHT 144
#define MENU_ITEMS 7
#define CONTEXT_ROW_H 20
#define WINDOW_RADIUS 7

static uint32_t text_len(const char* text) {
    uint32_t n = 0;
    while (text && text[n] && n < GUI_TEXT_MAX - 1) n++;
    return n;
}

static void text_copy(char* dst, uint32_t cap, const char* src) {
    if (!cap) return;
    uint32_t i = 0;
    while (src && src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void fill_clamped(GuiDesktop* desktop, int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= (int)desktop->width || y >= (int)desktop->height) return;
    if (x + w > (int)desktop->width) w = (int)desktop->width - x;
    if (y + h > (int)desktop->height) h = (int)desktop->height - y;
    if (w <= 0 || h <= 0) return;
    render_fill_rect(x, y, w, h, color);
}

static void border_clamped(GuiDesktop* desktop, int x, int y, int w, int h, uint32_t color) {
    fill_clamped(desktop, x, y, w, 1, color);
    fill_clamped(desktop, x, y + h - 1, w, 1, color);
    fill_clamped(desktop, x, y, 1, h, color);
    fill_clamped(desktop, x + w - 1, y, 1, h, color);
}

static int rounded_inset(int row, int height) {
    static const uint8_t inset[WINDOW_RADIUS] = { 5, 3, 2, 1, 1, 0, 0 };
    if (row < WINDOW_RADIUS) return inset[row];
    int from_bottom = height - 1 - row;
    if (from_bottom < WINDOW_RADIUS) return inset[from_bottom];
    return 0;
}

static void rounded_fill(GuiDesktop* desktop, int x, int y, int width, int height,
                         uint32_t color, int top_only) {
    if (width <= 0 || height <= 0) return;
    for (int row = 0; row < height; row++) {
        int inset = rounded_inset(row, height);
        if (top_only && row >= WINDOW_RADIUS) inset = 0;
        fill_clamped(desktop, x + inset, y + row, width - inset * 2, 1, color);
    }
}

GuiWindow* gui_window_find(GuiDesktop* desktop, int window_id) {
    if (!desktop || window_id < 0) return NULL;
    for (GuiWindowNode* n = desktop->windows; n; n = n->next) {
        if (n->id == window_id) return &n->w;
    }
    return NULL;
}

int gui_window_id(const GuiDesktop* desktop, const GuiWindow* w) {
    if (!desktop || !w) return -1;
    for (GuiWindowNode* n = desktop->windows; n; n = n->next) {
        if (&n->w == w) return n->id;
    }
    return -1;
}

static GuiWindow* get_window(GuiDesktop* desktop, int id) {
    return gui_window_find(desktop, id);
}

void gui_desktop_init(GuiDesktop* desktop, uint32_t width, uint32_t height) {
    if (!desktop) return;
    for (uint32_t i = 0; i < sizeof(*desktop); i++) ((uint8_t*)desktop)[i] = 0;
    desktop->width = width;
    desktop->height = height;
    desktop->focused_window = -1;
    desktop->next_z = 1;
    desktop->next_window_id = 0;
    desktop->windows = NULL;
    desktop->window_count = 0;
    desktop->mouse_x = (int)(width / 2);
    desktop->mouse_y = (int)(height / 2);
}

void gui_desktop_set_launch_action(GuiDesktop* desktop, GuiLaunchAction action) {
    if (desktop) desktop->launch_action = action;
}

void gui_desktop_set_theme(GuiDesktop* desktop, uint32_t background, uint32_t accent) {
    if (!desktop) return;
    desktop->background_color = background;
    desktop->accent_color = accent;
}

void gui_desktop_set_key_action(GuiDesktop* desktop, GuiKeyAction action, void* context) {
    if (!desktop) return;
    desktop->key_action = action;
    desktop->key_context = context;
}

void gui_window_set_list_action(GuiDesktop* desktop, int window_id,
                                GuiListAction action, void* context) {
    GuiWindow* window = get_window(desktop, window_id);
    if (!window) return;
    window->list_action = action;
    window->list_context = context;
}

void gui_window_set_list_activate_action(GuiDesktop* desktop, int window_id,
                                         GuiListAction action, void* context) {
    GuiWindow* window = get_window(desktop, window_id);
    if (!window) return;
    window->list_activate_action = action;
    window->list_activate_context = context;
}

void gui_window_set_focus_action(GuiDesktop* desktop, int window_id,
                                 GuiFocusAction action, void* context) {
    GuiWindow* window = get_window(desktop, window_id);
    if (!window) return;
    window->focus_action = action;
    window->focus_context = context;
    if (desktop->focused_window == window_id && action) action(context, 1);
}

void gui_window_set_close_action(GuiDesktop* desktop, int window_id,
                                 GuiCloseAction action, void* context) {
    GuiWindow* window = get_window(desktop, window_id);
    if (!window) return;
    window->close_action = action;
    window->close_context = context;
}

void gui_window_set_context_action(GuiDesktop* desktop, int window_id,
                                   GuiContextAction action, void* context) {
    GuiWindow* window = get_window(desktop, window_id);
    if (!window) return;
    window->context_action = action;
    window->context_context = context;
}

void gui_context_menu_show(GuiDesktop* desktop, int x, int y,
                           const char* const* items, uint32_t count,
                           int window_id, GuiMenuAction action, void* context) {
    if (!desktop || !items || !count) return;
    if (count > GUI_CONTEXT_ITEMS) count = GUI_CONTEXT_ITEMS;
    desktop->context_open = 1;
    desktop->context_x = x;
    desktop->context_y = y;
    desktop->context_window = window_id;
    desktop->context_count = count;
    desktop->context_action = action;
    desktop->context_context = context;
    for (uint32_t i = 0; i < count; i++) text_copy(desktop->context_items[i], sizeof(desktop->context_items[i]), items[i]);
}

int gui_window_create(GuiDesktop* desktop, const char* title, int x, int y,
                      int width, int height, GuiAction action, void* context) {
    if (!desktop || width < 100 || height < 64) return -1;

    if (desktop->focused_window >= 0) {
        GuiWindow* previous = gui_window_find(desktop, desktop->focused_window);
        if (previous && previous->focus_action) previous->focus_action(previous->focus_context, 0);
    }

    GuiWindowNode* node = (GuiWindowNode*)malloc(sizeof(GuiWindowNode));
    if (!node) return -1;
    memset(node, 0, sizeof(*node));
    node->id = desktop->next_window_id++;
    node->w.used = 1;
    node->w.visible = 1;
    node->w.x = x;
    node->w.y = y;
    node->w.width = width;
    node->w.height = height;
    node->w.normal_x = x;
    node->w.normal_y = y;
    node->w.normal_width = width;
    node->w.normal_height = height;
    node->w.z = desktop->next_z++;
    node->w.focused_control = -1;
    node->w.action = action;
    node->w.action_context = context;
    text_copy(node->w.title, sizeof(node->w.title), title);

    node->next = desktop->windows;
    desktop->windows = node;
    desktop->window_count++;
    desktop->focused_window = node->id;
    return node->id;
}

void gui_window_destroy(GuiDesktop* desktop, int window_id) {
    if (!desktop) return;
    GuiWindowNode** pp = &desktop->windows;
    while (*pp) {
        GuiWindowNode* n = *pp;
        if (n->id != window_id) { pp = &n->next; continue; }

        GuiWindow* window = &n->w;
        if (window->owner_pid > 0) {
            UserWindowMessage event = {0};
            event.type = USER_WINDOW_MSG_CLOSE;
            event.window_id = window_id;
            sys_window_reply(window->owner_pid, &event);
        }
        if (window->close_action) window->close_action(window->close_context);
        if (window->focus_action) window->focus_action(window->focus_context, 0);

        if (desktop->focused_window == window_id) desktop->focused_window = -1;
        *pp = n->next;
        free(n);
        desktop->window_count--;
        return;
    }
}

int gui_control_add(GuiDesktop* desktop, int window_id, GuiControlType type,
                    int x, int y, int width, int height, const char* text) {
    GuiWindow* window = get_window(desktop, window_id);
    if (!window || window->control_count >= GUI_MAX_CONTROLS || width <= 0 || height <= 0) return -1;
    int id = (int)window->control_count++;
    GuiControl* control = &window->controls[id];
    control->type = type;
    control->x = x;
    control->y = y;
    control->width = width;
    control->height = height;
    control->selected = -1;
    text_copy(control->text, sizeof(control->text), text);
    if (type == GUI_TEXTFIELD) control->cursor = (int)text_len(control->text);
    if (type == GUI_TEXTFIELD && window->focused_control < 0) {
        control->focused = 1;
        window->focused_control = id;
    }
    return id;
}

int gui_canvas_set_action(GuiDesktop* desktop, int window_id, int control_id,
                          GuiCanvasAction action, void* context) {
    GuiWindow* window = get_window(desktop, window_id);
    if (!window || control_id < 0 || (uint32_t)control_id >= window->control_count ||
        window->controls[control_id].type != GUI_CANVAS) return -1;
    window->controls[control_id].canvas_action = action;
    window->controls[control_id].canvas_context = context;
    return 0;
}

int gui_list_set_item(GuiDesktop* desktop, int window_id, int control_id,
                      uint32_t item, const char* text) {
    GuiWindow* window = get_window(desktop, window_id);
    if (!window || control_id < 0 || (uint32_t)control_id >= window->control_count || item >= GUI_LIST_CAPACITY) return -1;
    GuiControl* control = &window->controls[control_id];
    if (control->type != GUI_LIST) return -1;
    text_copy(control->items[item], sizeof(control->items[item]), text);
    if (item >= control->item_count) control->item_count = item + 1;
    return 0;
}

int gui_list_get_selected(const GuiDesktop* desktop, int window_id, int control_id) {
    if (!desktop || window_id < 0) return -1;
    const GuiWindow* window = gui_window_find((GuiDesktop*)desktop, window_id);
    if (!window || !window->used || control_id < 0 ||
        (uint32_t)control_id >= window->control_count) return -1;
    return window->controls[control_id].selected;
}

static void draw_control(GuiDesktop* desktop, const GuiControl* control, int x, int y) {
    switch (control->type) {
        case GUI_LABEL:
            render_draw_text(x, y, C_TEXT, C_PANEL, control->text);
            break;
        case GUI_BUTTON:
            fill_clamped(desktop, x, y, control->width, control->height, control->focused ? C_BUTTON_H : C_BUTTON);
            border_clamped(desktop, x, y, control->width, control->height, control->focused ? C_FOCUS : C_BORDER);
            render_draw_text(x + 8, y + (control->height - 8) / 2, C_TEXT,
                             control->focused ? C_BUTTON_H : C_BUTTON, control->text);
            break;
        case GUI_TEXTFIELD:
            fill_clamped(desktop, x, y, control->width, control->height, C_BG);
            border_clamped(desktop, x, y, control->width, control->height, control->focused ? C_FOCUS : C_BORDER);
            {
                uint32_t length = text_len(control->text);
                uint32_t visible_chars = control->width > 12 ? (uint32_t)(control->width - 12) / 8 : 0;
                uint32_t cursor = control->cursor < 0 ? 0 : (uint32_t)control->cursor;
                uint32_t first = cursor > visible_chars ? cursor - visible_chars : 0;
                if (first + visible_chars > length && length > visible_chars) first = length - visible_chars;
                char visible[GUI_TEXT_MAX];
                uint32_t count = length - first;
                if (count > visible_chars) count = visible_chars;
                for (uint32_t i = 0; i < count; i++) visible[i] = control->text[first + i];
                visible[count] = '\0';
                render_draw_text(x + 6, y + (control->height - 8) / 2, C_TEXT, C_BG, visible);
            }
            break;
        case GUI_TEXTAREA: {
            fill_clamped(desktop, x, y, control->width, control->height, C_BG);
            border_clamped(desktop, x, y, control->width, control->height, control->focused ? C_FOCUS : C_BORDER);
            int visible_rows = (control->height - 8) / 8;
            int total_rows = 1;
            int column = 0;
            for (uint32_t i = 0; control->text[i]; i++) {
                if (control->text[i] == '\r') continue;
                if (control->text[i] == '\n' || column >= (control->width - 12) / 8) {
                    total_rows++;
                    column = 0;
                    if (control->text[i] == '\n') continue;
                }
                column++;
            }
            int first_row = control->scroll ? total_rows - visible_rows : 0;
            if (first_row < 0) first_row = 0;
            int row = 0;
            column = 0;
            for (uint32_t i = 0; control->text[i] && row - first_row < visible_rows; i++) {
                if (control->text[i] == '\r') continue;
                if (control->text[i] == '\n' || column >= (control->width - 12) / 8) {
                    row++;
                    column = 0;
                    if (control->text[i] == '\n') continue;
                }
                if (row >= first_row) {
                    render_draw_text(x + 6 + column * 8, y + 4 + (row - first_row) * 8,
                                     C_TEXT, C_BG, (char[2]){ control->text[i], '\0' });
                }
                column++;
            }
            break;
        }
        case GUI_CANVAS:
            fill_clamped(desktop, x, y, control->width, control->height, C_BG);
            border_clamped(desktop, x, y, control->width, control->height, C_BORDER);
            if (control->canvas_action) {
                control->canvas_action(control->canvas_context, x + 1, y + 1,
                                       control->width - 2, control->height - 2);
            }
            break;
        case GUI_PANEL:
            fill_clamped(desktop, x, y, control->width, control->height, C_BG);
            border_clamped(desktop, x, y, control->width, control->height, C_BORDER);
            break;
        case GUI_LIST: {
            fill_clamped(desktop, x, y, control->width, control->height, C_BG);
            border_clamped(desktop, x, y, control->width, control->height, C_BORDER);
            int visible_rows = (control->height - 4) / 16;
            int track_height = control->height - 4;
            int content_width = control->width - 14;
            if (visible_rows < 1) visible_rows = 1;
            int max_scroll = (int)control->item_count - visible_rows;
            if (max_scroll < 0) max_scroll = 0;
            int scroll = control->scroll;
            if (scroll < 0) scroll = 0;
            if (scroll > max_scroll) scroll = max_scroll;
            for (int row = 0; row < visible_rows; row++) {
                uint32_t item = (uint32_t)(scroll + row);
                if (item >= control->item_count || y + 2 + (row + 1) * 16 > y + control->height - 2) break;
                int row_y = y + 2 + row * 16;
                uint32_t bg = ((int)item == control->selected) ? C_TITLE_F : C_BG;
                fill_clamped(desktop, x + 2, row_y, content_width, 16, bg);
                render_draw_text(x + 6, row_y + 4, C_TEXT, bg, control->items[item]);
            }
            fill_clamped(desktop, x + control->width - 10, y + 2, 6, track_height, C_PANEL);
            if (control->item_count > (uint32_t)visible_rows) {
                int thumb_height = track_height * visible_rows / (int)control->item_count;
                if (thumb_height < 12) thumb_height = 12;
                int thumb_range = track_height - thumb_height;
                int thumb_y = y + 2 + (max_scroll ? thumb_range * scroll / max_scroll : 0);
                uint32_t accent = desktop->accent_color ? desktop->accent_color : C_TITLE_F;
                fill_clamped(desktop, x + control->width - 10, thumb_y, 6, thumb_height, accent);
            }
            break;
        }
    }
}

static void draw_window(GuiDesktop* desktop, GuiWindow* window) {
    if (!window->used || !window->visible) return;
    uint32_t title_color = desktop->focused_window >= 0 &&
                           gui_window_find(desktop, desktop->focused_window) == window ? C_TITLE_F : C_TITLE;
    uint32_t border_color = desktop->focused_window >= 0 &&
                            gui_window_find(desktop, desktop->focused_window) == window ? C_TITLE_F : C_BORDER;
    rounded_fill(desktop, window->x + 3, window->y + 3, window->width, window->height, C_BLACK, 0);
    rounded_fill(desktop, window->x, window->y, window->width, window->height, border_color, 0);
    rounded_fill(desktop, window->x + 1, window->y + 1, window->width - 2,
                 window->height - 2, C_PANEL, 0);
    rounded_fill(desktop, window->x + 1, window->y + 1, window->width - 2,
                 TITLE_H, title_color, 1);
    int buttons_x = window->x + window->width - 3 * TITLE_BUTTON - 4;
    render_draw_text(window->x + 8, window->y + 8, C_TEXT, title_color, window->title);
    fill_clamped(desktop, buttons_x, window->y + 3, TITLE_BUTTON - 1, TITLE_H - 6, C_MINIMIZE);
    fill_clamped(desktop, buttons_x + TITLE_BUTTON, window->y + 3, TITLE_BUTTON - 1, TITLE_H - 6, C_MAXIMIZE);
    fill_clamped(desktop, buttons_x + 2 * TITLE_BUTTON, window->y + 3, TITLE_BUTTON - 1, TITLE_H - 6, C_CLOSE);
    fill_clamped(desktop, buttons_x + 5, window->y + 14, 8, 2, C_WHITE);
    border_clamped(desktop, buttons_x + TITLE_BUTTON + 5, window->y + 6, 9, 9, C_WHITE);
    for (int i = 0; i < 8; i++) {
        fill_clamped(desktop, buttons_x + 2 * TITLE_BUTTON + 5 + i,
                     window->y + 7 + i, 2, 2, C_WHITE);
        fill_clamped(desktop, buttons_x + 2 * TITLE_BUTTON + 11 - i,
                     window->y + 7 + i, 2, 2, C_WHITE);
    }
    for (uint32_t i = 0; i < window->control_count; i++) {
        GuiControl* control = &window->controls[i];
        draw_control(desktop, control, window->x + control->x, window->y + TITLE_H + control->y);
    }
}

void gui_desktop_draw(GuiDesktop* desktop) {
    if (!desktop) return;
    fill_clamped(desktop, 0, 0, (int)desktop->width, (int)desktop->height - TASKBAR_H,
                 desktop->background_color ? desktop->background_color : C_DESK);
    for (int z = 1; z < desktop->next_z; z++) {
        for (GuiWindowNode* n = desktop->windows; n; n = n->next) {
            if (n->w.used && n->w.visible && !n->w.minimized && n->w.z == z) {
                draw_window(desktop, &n->w);
            }
        }
    }
    fill_clamped(desktop, 0, (int)desktop->height - TASKBAR_H, (int)desktop->width, TASKBAR_H, C_BG);
    uint32_t accent = desktop->accent_color ? desktop->accent_color : C_TITLE_F;
    fill_clamped(desktop, 8, (int)desktop->height - 24, 72, 20, accent);
    render_draw_text(20, (int)desktop->height - 18, C_WHITE, accent, "LiveOS");
    int task_x = 88;
    for (GuiWindowNode* n = desktop->windows; n; n = n->next) {
        GuiWindow* window = &n->w;
        if (!window->used) continue;
        fill_clamped(desktop, task_x, (int)desktop->height - 24, 104, 20,
                 desktop->focused_window == n->id && !window->minimized ? C_BUTTON_H : C_BUTTON);
        render_draw_text(task_x + 5, (int)desktop->height - 18, C_TEXT,
                 desktop->focused_window == n->id && !window->minimized ? C_BUTTON_H : C_BUTTON,
                 window->title);
        task_x += 108;
    }
    if (desktop->start_open) {
        int menu_y = (int)desktop->height - TASKBAR_H - MENU_HEIGHT;
        static const char* defaults[MENU_ITEMS] = {
            "Terminal", "Files", "Notepad", "Registry", "Settings", "Fonts", "UI Studio"
        };
        char apps[MENU_ITEMS][32];
        for (int i = 0; i < MENU_ITEMS; i++) {
            text_copy(apps[i], sizeof(apps[i]), defaults[i]);
            char key[32];
            text_copy(key, sizeof(key), "USER/startmenu/");
            key[15] = (char)('0' + i);
            key[16] = '\0';
            char value[32];
            if (sys_reg_get_str(key, value, sizeof(value)) > 0 && value[0]) {
                text_copy(apps[i], sizeof(apps[i]), value);
            }
        }
        fill_clamped(desktop, 8, menu_y, 176, MENU_HEIGHT, C_PANEL);
        border_clamped(desktop, 8, menu_y, 176, MENU_HEIGHT, C_BORDER);
        render_draw_text(18, menu_y + 10, C_TEXT, C_PANEL, "Applications");
        for (int i = 0; i < MENU_ITEMS; i++) {
            int row_y = menu_y + 28 + i * 18;
            int hovered = desktop->mouse_x >= 14 && desktop->mouse_x < 178 &&
                          desktop->mouse_y >= row_y && desktop->mouse_y < row_y + 18;
            uint32_t row_bg = hovered ? C_BUTTON_H : C_PANEL;
            if (hovered) {
                fill_clamped(desktop, 14, row_y, 164, 18, C_BUTTON_H);
            }
            render_draw_text(22, row_y + 5, C_TEXT, row_bg, apps[i]);
        }
    }
    if (desktop->context_open) {
        uint32_t width = 168;
        int height = (int)desktop->context_count * CONTEXT_ROW_H + 4;
        fill_clamped(desktop, desktop->context_x, desktop->context_y, (int)width, height, C_PANEL);
        border_clamped(desktop, desktop->context_x, desktop->context_y, (int)width, height, C_BORDER);
        for (uint32_t i = 0; i < desktop->context_count; i++) {
            int row_y = desktop->context_y + 2 + (int)i * CONTEXT_ROW_H;
            int hovered = desktop->mouse_x >= desktop->context_x && desktop->mouse_x < desktop->context_x + (int)width &&
                          desktop->mouse_y >= row_y && desktop->mouse_y < row_y + CONTEXT_ROW_H;
            uint32_t bg = hovered ? C_BUTTON_H : C_PANEL;
            if (hovered) fill_clamped(desktop, desktop->context_x + 1, row_y, (int)width - 2, CONTEXT_ROW_H, bg);
            render_draw_text(desktop->context_x + 8, row_y + 6, C_TEXT, bg, desktop->context_items[i]);
        }
    }
}

void gui_desktop_draw_excluding(GuiDesktop* desktop, int excluded_window) {
    if (!desktop) return;
    GuiWindow* excluded = (excluded_window >= 0)
        ? gui_window_find(desktop, excluded_window) : NULL;
    int visible = excluded ? excluded->visible : 0;
    if (excluded) excluded->visible = 0;
    gui_desktop_draw(desktop);
    if (excluded) excluded->visible = visible;
}

void gui_desktop_draw_window(GuiDesktop* desktop, int window_id) {
    if (!desktop || window_id < 0) return;
    GuiWindow* window = gui_window_find(desktop, window_id);
    if (window && window->used && window->visible) draw_window(desktop, window);
}

static int window_at(GuiDesktop* desktop, int x, int y) {
    int found = -1;
    int top_z = -1;
    for (GuiWindowNode* n = desktop->windows; n; n = n->next) {
        GuiWindow* window = &n->w;
        if (!window->used || !window->visible || window->minimized) continue;
        if (x >= window->x && x < window->x + window->width &&
            y >= window->y && y < window->y + window->height && window->z > top_z) {
            found = n->id;
            top_z = window->z;
        }
    }
    return found;
}

static int control_at(GuiWindow* window, int x, int y) {
    for (int i = (int)window->control_count - 1; i >= 0; i--) {
        GuiControl* control = &window->controls[i];
        int left = window->x + control->x;
        int top = window->y + TITLE_H + control->y;
        if (x >= left && x < left + control->width && y >= top && y < top + control->height) return i;
    }
    return -1;
}

static void focus_window(GuiDesktop* desktop, int id) {
    GuiWindow* target = gui_window_find(desktop, id);
    if (!target || !target->used) return;
    if (desktop->focused_window >= 0 && desktop->focused_window != id) {
        GuiWindow* old = gui_window_find(desktop, desktop->focused_window);
        if (old && old->used && old->focus_action) old->focus_action(old->focus_context, 0);
    }
    desktop->focused_window = id;
    target->visible = 1;
    target->minimized = 0;
    target->z = desktop->next_z++;
    if (target->focus_action) target->focus_action(target->focus_context, 1);
}

static void maximize_window(GuiDesktop* desktop, GuiWindow* window) {
    if (!window->maximized) {
        window->normal_x = window->x;
        window->normal_y = window->y;
        window->normal_width = window->width;
        window->normal_height = window->height;
        window->x = 0;
        window->y = 0;
        window->width = (int)desktop->width;
        window->height = (int)desktop->height - TASKBAR_H;
        window->maximized = 1;
    } else {
        window->x = window->normal_x;
        window->y = window->normal_y;
        window->width = window->normal_width;
        window->height = window->normal_height;
        window->maximized = 0;
    }
}

void gui_desktop_handle_pointer(GuiDesktop* desktop, int x, int y, uint32_t buttons) {
    if (!desktop) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= (int)desktop->width) x = (int)desktop->width - 1;
    if (y >= (int)desktop->height) y = (int)desktop->height - 1;
    desktop->mouse_x = x;
    desktop->mouse_y = y;

    int pressed = (buttons & 1u) && !(desktop->mouse_buttons & 1u);
    int right_pressed = (buttons & 2u) && !(desktop->mouse_buttons & 2u);
    int released = !(buttons & 1u) && (desktop->mouse_buttons & 1u);
    desktop->old_buttons = desktop->mouse_buttons;
    desktop->mouse_buttons = buttons;
    if (right_pressed) {
        int id = window_at(desktop, x, y);
        if (id >= 0) {
            GuiWindow* window = gui_window_find(desktop, id);
            focus_window(desktop, id);
            if (window->context_action) {
                window->context_action(window->context_context, x, y);
            } else {
                static const char* items[] = { "Minimize", "Maximize / Restore", "Close" };
                gui_context_menu_show(desktop, x, y, items, 3, id, NULL, NULL);
            }
        } else {
            desktop->context_open = 0;
        }
        return;
    }
    if (pressed) {
        if (desktop->context_open) {
            int menu_x = desktop->context_x;
            int menu_y = desktop->context_y;
            int item = (y - menu_y - 2) / CONTEXT_ROW_H;
            int inside = x >= menu_x && x < menu_x + 168 && item >= 0 && (uint32_t)item < desktop->context_count;
            GuiMenuAction action = desktop->context_action;
            void* context = desktop->context_context;
            int context_window = desktop->context_window;
            desktop->context_open = 0;
            if (inside) {
                if (action) action(context, item);
                else {
                        GuiWindow* target = (context_window >= 0)
                                        ? gui_window_find(desktop, context_window) : NULL;
                    if (item == 0) {
                        target->minimized = 1;
                        target->visible = 0;
                        if (target->focus_action) target->focus_action(target->focus_context, 0);
                        desktop->focused_window = -1;
                    } else if (item == 1) maximize_window(desktop, target);
                    else if (item == 2) gui_window_destroy(desktop, context_window);
                }
            }
            return;
        }
        int taskbar_y = (int)desktop->height - TASKBAR_H;
        if (y >= taskbar_y && x < 82) {
            desktop->start_open = !desktop->start_open;
            return;
        }
        if (y >= taskbar_y) {
            if (x < 88) return;
            int item = (x - 88) / 108;
            int seen = 0;
            for (GuiWindowNode* n = desktop->windows; n; n = n->next) {
                if (!n->w.used) continue;
                if (seen++ == item) {
                    if (desktop->focused_window == n->id && !n->w.minimized) {
                        n->w.minimized = 1;
                        n->w.visible = 0;
                        if (n->w.focus_action) n->w.focus_action(n->w.focus_context, 0);
                        desktop->focused_window = -1;
                    } else {
                        focus_window(desktop, n->id);
                    }
                    return;
                }
            }
        }
        if (desktop->start_open && y < taskbar_y && x < 184) {
            int menu_y = (int)desktop->height - TASKBAR_H - MENU_HEIGHT;
            int row = (y - menu_y - 28) / 18;
            desktop->start_open = 0;
            if (x >= 14 && row >= 0 && row < MENU_ITEMS && desktop->launch_action) {
                desktop->launch_action(row);
            }
            return;
        }
        int id = window_at(desktop, x, y);
        if (id < 0) {
            if (desktop->focused_window >= 0) {
                GuiWindow* old = gui_window_find(desktop, desktop->focused_window);
                if (old && old->used && old->focus_action) old->focus_action(old->focus_context, 0);
            }
            desktop->focused_window = -1;
            return;
        }
        focus_window(desktop, id);
        GuiWindow* window = gui_window_find(desktop, id);
        if (y < window->y + TITLE_H) {
            int buttons_x = window->x + window->width - 3 * TITLE_BUTTON - 4;
            if (x >= buttons_x + 2 * TITLE_BUTTON) {
                gui_window_destroy(desktop, id);
                return;
            }
            if (x >= buttons_x + TITLE_BUTTON) {
                maximize_window(desktop, window);
                return;
            }
            if (x >= buttons_x) {
                window->minimized = 1;
                window->visible = 0;
                if (window->focus_action) window->focus_action(window->focus_context, 0);
                desktop->focused_window = -1;
                return;
            }
            if (window->maximized) return;
            window->dragging = 1;
            window->drag_dx = x - window->x;
            window->drag_dy = y - window->y;
            return;
        }
        int control_id = control_at(window, x, y);
        if (control_id >= 0) {
            for (uint32_t i = 0; i < window->control_count; i++) window->controls[i].focused = 0;
            window->focused_control = control_id;
            window->controls[control_id].focused = 1;
            GuiControl* control = &window->controls[control_id];
            if (window->owner_pid > 0) {
                if (control->type == GUI_LIST) {
                    int local_y = y - (window->y + TITLE_H + control->y);
                    int visible_rows = (control->height - 4) / 16;
                    if (visible_rows < 1) visible_rows = 1;
                    int row = (local_y - 2) / 16;
                    int selected = control->scroll + row;
                    if (row >= 0 && row < visible_rows && selected < (int)control->item_count) {
                        control->selected = selected;
                    }
                }
                UserWindowMessage event = {0};
                event.type = USER_WINDOW_MSG_INPUT;
                event.window_id = desktop->focused_window;
                event.control_id = window->focused_control;
                event.input_type = USER_WINDOW_INPUT_POINTER;
                event.x = x - window->x;
                event.y = y - window->y - 24;
                event.selected = control->selected;
                sys_window_reply(window->owner_pid, &event);
            } else {
                if (control->type == GUI_LIST) {
                    int local_y = y - (window->y + TITLE_H + control->y);
                    int visible_rows = (control->height - 4) / 16;
                    if (visible_rows < 1) visible_rows = 1;
                    int row = (local_y - 2) / 16;
                    int selected = control->scroll + row;
                    if (row >= 0 && row < visible_rows && selected < (int)control->item_count) {
                        control->selected = selected;
                        if (window->list_action) window->list_action(window->list_context, control);
                    }
                }
                if (control->type == GUI_BUTTON && window->action) window->action(window->action_context, control);
            }
        }
    }
    if (released) {
        for (GuiWindowNode* n = desktop->windows; n; n = n->next) n->w.dragging = 0;
    }
    if (buttons & 1u) {
        for (GuiWindowNode* n = desktop->windows; n; n = n->next) {
            GuiWindow* window = &n->w;
            if (window->used && window->dragging) {
                window->x = x - window->drag_dx;
                window->y = y - window->drag_dy;
            }
        }
    }
}

void gui_desktop_handle_key(GuiDesktop* desktop, int key) {
    if (!desktop) return;
    if (desktop->key_action) desktop->key_action(desktop->key_context, key);
    if (key == 27) { desktop->start_open = 0; return; }
    if (desktop->focused_window < 0) return;
    GuiWindow* window = gui_window_find(desktop, desktop->focused_window);
    if (!window || !window->used || window->focused_control < 0 ||
        (uint32_t)window->focused_control >= window->control_count) return;
    GuiControl* control = &window->controls[window->focused_control];
    if (window->owner_pid > 0) {
        UserWindowMessage event = {0};
        event.type = USER_WINDOW_MSG_INPUT;
        event.window_id = desktop->focused_window;
        event.control_id = window->focused_control;
        event.input_type = USER_WINDOW_INPUT_KEY;
        event.input_key = key;
        event.selected = control->selected;
        sys_window_reply(window->owner_pid, &event);
        return;
    }
    if (key == '\n' && control->type != GUI_TEXTAREA) {
        if (control->type == GUI_LIST && window->list_activate_action) {
            window->list_activate_action(window->list_activate_context, control);
            return;
        }
        if (window->action && (control->type == GUI_TEXTFIELD || control->type == GUI_BUTTON)) {
            window->action(window->action_context, control);
            return;
        }
    }
    if (key == '\t') {
        for (uint32_t i = 0; i < window->control_count; i++) window->controls[i].focused = 0;
        for (uint32_t n = 0; n < window->control_count; n++) {
            window->focused_control = (window->focused_control + 1) % (int)window->control_count;
            if (window->controls[window->focused_control].type != GUI_LABEL &&
                window->controls[window->focused_control].type != GUI_PANEL) {
                window->controls[window->focused_control].focused = 1;
                return;
            }
        }
        return;
    }
    if (control->type == GUI_TEXTFIELD || control->type == GUI_TEXTAREA) {
        uint32_t length = text_len(control->text);
        if (key == '\b') {
            if (control->cursor > 0 && (uint32_t)control->cursor <= length) {
                uint32_t cursor = (uint32_t)--control->cursor;
                for (uint32_t i = cursor; i < length; i++) control->text[i] = control->text[i + 1];
            }
        } else if (key == KBD_KEY_LEFT && control->cursor > 0) {
            control->cursor--;
        } else if (key == KBD_KEY_RIGHT && (uint32_t)control->cursor < length) {
            control->cursor++;
        } else if (key == KBD_KEY_DELETE && (uint32_t)control->cursor < length) {
            for (uint32_t i = (uint32_t)control->cursor; i < length; i++) control->text[i] = control->text[i + 1];
        } else if ((key == '\n' && control->type == GUI_TEXTAREA) ||
                   (key >= 32 && key < 127)) {
            if (length + 1 >= GUI_TEXT_MAX) return;
            uint32_t cursor = control->cursor < 0 ? 0 : (uint32_t)control->cursor;
            if (cursor > length) cursor = length;
            for (uint32_t i = length + 1; i > cursor; i--) control->text[i] = control->text[i - 1];
            control->text[cursor] = (char)key;
            control->cursor++;
        }
    } else if (control->type == GUI_LIST) {
        int visible_rows = (control->height - 4) / 16;
        if (visible_rows < 1) visible_rows = 1;
        if (key == KBD_KEY_UP) {
            if (control->selected > 0) control->selected--;
            else if (control->selected < 0) control->selected = 0;
            if (control->selected < control->scroll) control->scroll = control->selected;
        }
        if (key == KBD_KEY_DOWN && control->selected < (int)control->item_count - 1) control->selected++;
        if (control->selected >= control->scroll + visible_rows) control->scroll = control->selected - visible_rows + 1;
    } else if (control->type == GUI_BUTTON && key == '\n' && window->action) {
        window->action(window->action_context, control);
    }
}

int gui_desktop_poll(GuiDesktop* desktop) {
    if (!desktop) return -1;
    UserInputState input;
    if (sys_input_poll(&input) != 0) return -1;
    int changed = input.pointer_buttons != desktop->mouse_buttons || input.key != 0;
    gui_desktop_handle_pointer(desktop, input.pointer_x, input.pointer_y, input.pointer_buttons);
    if (input.key) gui_desktop_handle_key(desktop, input.key);
    return changed;
}

int gui_app_init(GuiAppContext* app) {
    if (!app) return -1;
    app->width = 1024;
    app->height = 768;
    app->framebuffer = NULL;
    gui_desktop_init(&app->desktop, app->width, app->height);
    return 0;
}

int gui_app_create_window(GuiAppContext* app, const char* title,
                          int x, int y, int width, int height) {
    if (!app) return -1;
    return gui_window_create(&app->desktop, title, x, y, width, height, NULL, NULL);
}

void gui_app_draw(GuiAppContext* app) {
    if (!app || !app->framebuffer) return;
    gui_desktop_draw(&app->desktop);
    render_present();
}

int gui_app_add_control(GuiAppContext* app, int window_id, GuiControlType type,
                        int x, int y, int width, int height, const char* text) {
    if (!app) return -1;
    return gui_control_add(&app->desktop, window_id, type, x, y, width, height, text);
}

void gui_app_set_action(GuiAppContext* app, int window_id, GuiAction action, void* context) {
    if (!app || window_id < 0) return;
    GuiWindow* window = gui_window_find(&app->desktop, window_id);
    if (!window || !window->used) return;
    window->action = action;
    window->action_context = context;
}

void gui_app_set_list_action(GuiAppContext* app, int window_id,
                             GuiListAction action, void* context) {
    if (app) gui_window_set_list_action(&app->desktop, window_id, action, context);
}

void gui_app_set_list_activate_action(GuiAppContext* app, int window_id,
                                      GuiListAction action, void* context) {
    if (app) gui_window_set_list_activate_action(&app->desktop, window_id, action, context);
}

void gui_app_set_close_action(GuiAppContext* app, int window_id,
                              GuiCloseAction action, void* context) {
    if (app) gui_window_set_close_action(&app->desktop, window_id, action, context);
}

int gui_app_poll(GuiAppContext* app) {
    if (!app || !app->framebuffer) return 0;
    int changed = gui_desktop_poll(&app->desktop);
    if (changed > 0) {
        gui_desktop_draw(&app->desktop);
        render_present();
    }
    return changed;
}
