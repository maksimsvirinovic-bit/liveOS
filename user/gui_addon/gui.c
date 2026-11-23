#include "gui_addon/gui.h"
#include "../sys.h"
#include "render.h"
#include <stddef.h>

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
#define TITLE_BUTTON 18
#define CONTEXT_ROW_H 20
#define WINDOW_RADIUS 7

static char* gui_strdup(const char* s) {
    if (!s) return NULL;
    uint32_t n = 0;
    while (s[n]) n++;
    char* p = (char*)malloc(n + 1);
    if (!p) return NULL;
    for (uint32_t i = 0; i <= n; i++) p[i] = s[i];
    return p;
}

static void gui_strfree(char** p) {
    if (*p) { free(*p); *p = NULL; }
}

static uint32_t gui_strlen(const char* s) {
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

void gui_desktop_init(GuiDesktop* d, uint32_t w, uint32_t h) {
    if (!d) return;
    for (uint32_t i = 0; i < sizeof(*d); i++) ((uint8_t*)d)[i] = 0;
    d->width = w;
    d->height = h;
    d->focused_window = -1;
    d->next_z = 1;
    d->next_window_id = 0;
    d->mouse_x = (int)(w / 2);
    d->mouse_y = (int)(h / 2);
}

static void gui_control_list_destroy(GuiControl* c) {
    while (c) {
        GuiControl* next = c->next;
        if (c->text) free(c->text);
        if (c->items) {
            for (uint32_t i = 0; i < c->item_count; i++) {
                if (c->items[i]) free(c->items[i]);
            }
            free(c->items);
        }
        free(c);
        c = next;
    }
}

static void gui_window_list_destroy(GuiWindow* w) {
    while (w) {
        GuiWindow* next = w->next;
        gui_control_list_destroy(w->controls);
        free(w);
        w = next;
    }
}

void gui_desktop_destroy(GuiDesktop* d) {
    if (!d) return;
    gui_window_list_destroy(d->windows);
    d->windows = NULL;
    d->window_count = 0;
    gui_start_menu_clear(d);
    if (d->context_items) {
        for (uint32_t i = 0; i < d->context_count; i++) {
            if (d->context_items[i]) free(d->context_items[i]);
        }
        free(d->context_items);
        d->context_items = NULL;
    }
    d->context_count = d->context_capacity = 0;
    if (d->animations) { free(d->animations); d->animations = NULL; }
    d->animation_count = d->animation_capacity = 0;
}

void gui_desktop_set_theme(GuiDesktop* d, uint32_t bg, uint32_t accent) {
    if (!d) return;
    d->background_color = bg;
    d->accent_color = accent;
}

void gui_desktop_set_launch_action(GuiDesktop* d, GuiLaunchAction a, void* ctx) {
    if (!d) return;
    d->launch_action = a;
    d->launch_context = ctx;
}

void gui_desktop_set_key_action(GuiDesktop* d, GuiKeyAction a, void* ctx) {
    if (!d) return;
    d->key_action = a;
    d->key_context = ctx;
}

GuiWindow* gui_window_find(GuiDesktop* d, int id) {
    if (!d || id < 0) return NULL;
    for (GuiWindow* w = d->windows; w; w = w->next) {
        if (w->id == id) return w;
    }
    return NULL;
}

int gui_window_create(GuiDesktop* d, const char* title, int x, int y, int w, int h) {
    if (!d || w < 100 || h < 64) return -1;
    GuiWindow* win = (GuiWindow*)calloc(1, sizeof(GuiWindow));
    if (!win) return -1;
    win->id = d->next_window_id++;
    win->used = 1;
    win->visible = 1;
    win->x = x; win->y = y; win->width = w; win->height = h;
    win->normal_x = x; win->normal_y = y;
    win->normal_width = w; win->normal_height = h;
    win->z = d->next_z++;
    win->focused_control = -1;
    win->next_control_id = 0;
    if (title) {
        uint32_t i = 0;
        while (title[i] && i + 1 < sizeof(win->title)) { win->title[i] = title[i]; i++; }
        win->title[i] = '\0';
    }
    win->next = d->windows;
    d->windows = win;
    d->window_count++;
    d->focused_window = win->id;
    return win->id;
}

void gui_window_destroy(GuiDesktop* d, int id) {
    if (!d) return;
    GuiWindow** pp = &d->windows;
    while (*pp) {
        GuiWindow* w = *pp;
        if (w->id != id) { pp = &w->next; continue; }
        if (w->owner_pid > 0) {
            UserWindowMessage ev = {0};
            ev.type = USER_WINDOW_MSG_CLOSE;
            ev.window_id = id;
            sys_window_reply(w->owner_pid, &ev);
        }
        if (w->close_action) w->close_action(w->close_context);
        if (w->focus_action) w->focus_action(w->focus_context, 0);
        if (d->focused_window == id) d->focused_window = -1;
        gui_control_list_destroy(w->controls);
        *pp = w->next;
        free(w);
        d->window_count--;
        return;
    }
}

void gui_window_set_close_action(GuiDesktop* d, int id, GuiCloseAction a, void* ctx) {
    GuiWindow* w = gui_window_find(d, id);
    if (w) { w->close_action = a; w->close_context = ctx; }
}
void gui_window_set_focus_action(GuiDesktop* d, int id, GuiFocusAction a, void* ctx) {
    GuiWindow* w = gui_window_find(d, id);
    if (w) { w->focus_action = a; w->focus_context = ctx; }
}
void gui_window_set_context_action(GuiDesktop* d, int id, GuiContextAction a, void* ctx) {
    GuiWindow* w = gui_window_find(d, id);
    if (w) { w->context_action = a; w->context_context = ctx; }
}
void gui_window_set_list_action(GuiDesktop* d, int id, GuiListAction a, void* ctx) {
    GuiWindow* w = gui_window_find(d, id);
    if (w) { w->list_action = a; w->list_context = ctx; }
}
void gui_window_set_list_activate_action(GuiDesktop* d, int id, GuiListAction a, void* ctx) {
    GuiWindow* w = gui_window_find(d, id);
    if (w) { w->list_activate_action = a; w->list_activate_context = ctx; }
}

GuiControl* gui_control_find(GuiDesktop* d, int window_id, int control_id) {
    GuiWindow* w = gui_window_find(d, window_id);
    if (!w || control_id < 0) return NULL;
    for (GuiControl* c = w->controls; c; c = c->next) {
        if (c->id == control_id) return c;
    }
    return NULL;
}

int gui_control_add(GuiDesktop* d, int window_id, GuiControlType type,
                    int x, int y, int w, int h, const char* text) {
    GuiWindow* win = gui_window_find(d, window_id);
    if (!win || w <= 0 || h <= 0) return -1;
    GuiControl* c = (GuiControl*)calloc(1, sizeof(GuiControl));
    if (!c) return -1;
    c->type = type;
    c->id = win->next_control_id++;
    c->x = x; c->y = y; c->width = w; c->height = h;
    c->visible = 1;
    c->selected = -1;
    c->font_scale = 1;
    c->item_text_max = 96;
    if (text && text[0]) {
        c->text = gui_strdup(text);
        c->text_capacity = c->text ? gui_strlen(text) + 1 : 0;
        if (type == GUI_TEXTFIELD) c->cursor = (int)gui_strlen(c->text);
    }
    if (type == GUI_TEXTFIELD && win->focused_control < 0) {
        c->focused = 1;
        win->focused_control = c->id;
    }
    c->next = win->controls;
    win->controls = c;
    win->control_count++;
    return c->id;
}

void gui_control_destroy(GuiDesktop* d, int window_id, int control_id) {
    GuiWindow* w = gui_window_find(d, window_id);
    if (!w) return;
    GuiControl** pp = &w->controls;
    while (*pp) {
        GuiControl* c = *pp;
        if (c->id != control_id) { pp = &c->next; continue; }
        if (c->text) free(c->text);
        if (c->items) {
            for (uint32_t i = 0; i < c->item_count; i++) {
                if (c->items[i]) free(c->items[i]);
            }
            free(c->items);
        }
        if (w->focused_control == control_id) w->focused_control = -1;
        *pp = c->next;
        free(c);
        w->control_count--;
        return;
    }
}

int gui_control_set_text(GuiDesktop* d, int window_id, int control_id, const char* text) {
    GuiControl* c = gui_control_find(d, window_id, control_id);
    if (!c) return -1;
    uint32_t n = text ? gui_strlen(text) : 0;
    if (n + 1 > c->text_capacity) {
        char* np = (char*)malloc(n + 1);
        if (!np) return -1;
        if (c->text) free(c->text);
        c->text = np;
        c->text_capacity = n + 1;
    }
    if (c->text) {
        for (uint32_t i = 0; i <= n; i++) c->text[i] = text ? text[i] : '\0';
    }
    if (c->type == GUI_TEXTFIELD) c->cursor = (int)n;
    return 0;
}

const char* gui_control_get_text(GuiDesktop* d, int window_id, int control_id) {
    GuiControl* c = gui_control_find(d, window_id, control_id);
    return c ? (c->text ? c->text : "") : "";
}

void gui_control_set_action(GuiDesktop* d, int window_id, int control_id,
                            GuiControlAction a, void* ctx) {
    GuiControl* c = gui_control_find(d, window_id, control_id);
    if (c) { c->action = a; c->action_context = ctx; }
}

void gui_control_set_canvas_action(GuiDesktop* d, int window_id, int control_id,
                                   GuiCanvasAction a, void* ctx) {
    GuiControl* c = gui_control_find(d, window_id, control_id);
    if (c && c->type == GUI_CANVAS) { c->canvas_action = a; c->canvas_context = ctx; }
}

void gui_control_set_colors(GuiDesktop* d, int window_id, int control_id,
                            uint32_t fg, uint32_t bg, uint32_t border) {
    GuiControl* c = gui_control_find(d, window_id, control_id);
    if (c) { c->fg_color = fg; c->bg_color = bg; c->border_color = border; }
}

int gui_list_add_item(GuiDesktop* d, int window_id, int control_id, const char* text) {
    GuiControl* c = gui_control_find(d, window_id, control_id);
    if (!c || c->type != GUI_LIST) return -1;
    if (c->item_count >= c->item_capacity) {
        uint32_t newcap = c->item_capacity ? c->item_capacity * 2 : 8;
        char** ni = (char**)realloc(c->items, newcap * sizeof(char*));
        if (!ni) return -1;
        for (uint32_t i = c->item_capacity; i < newcap; i++) ni[i] = NULL;
        c->items = ni;
        c->item_capacity = newcap;
    }
    c->items[c->item_count] = gui_strdup(text ? text : "");
    if (!c->items[c->item_count]) return -1;
    return (int)c->item_count++;
}

int gui_list_set_item(GuiDesktop* d, int window_id, int control_id,
                      uint32_t index, const char* text) {
    GuiControl* c = gui_control_find(d, window_id, control_id);
    if (!c || c->type != GUI_LIST) return -1;
    while (index >= c->item_capacity) {
        uint32_t newcap = c->item_capacity ? c->item_capacity * 2 : 8;
        char** ni = (char**)realloc(c->items, newcap * sizeof(char*));
        if (!ni) return -1;
        for (uint32_t i = c->item_capacity; i < newcap; i++) ni[i] = NULL;
        c->items = ni;
        c->item_capacity = newcap;
    }
    if (c->items[index]) free(c->items[index]);
    c->items[index] = gui_strdup(text ? text : "");
    if (!c->items[index]) return -1;
    if (index >= c->item_count) c->item_count = index + 1;
    return 0;
}

void gui_list_clear(GuiDesktop* d, int window_id, int control_id) {
    GuiControl* c = gui_control_find(d, window_id, control_id);
    if (!c || c->type != GUI_LIST) return;
    for (uint32_t i = 0; i < c->item_count; i++) {
        if (c->items[i]) { free(c->items[i]); c->items[i] = NULL; }
    }
    c->item_count = 0;
    c->selected = -1;
    c->scroll = 0;
}

int gui_list_get_selected(GuiDesktop* d, int window_id, int control_id) {
    GuiControl* c = gui_control_find(d, window_id, control_id);
    return c ? c->selected : -1;
}

void gui_context_menu_show(GuiDesktop* d, int x, int y,
                           const char* const* items, uint32_t count,
                           int window_id, GuiMenuAction action, void* ctx) {
    if (!d || !items || !count) return;
    if (d->context_items) {
        for (uint32_t i = 0; i < d->context_count; i++) {
            if (d->context_items[i]) free(d->context_items[i]);
        }
        free(d->context_items);
        d->context_items = NULL;
    }
    d->context_items = (char**)calloc(count, sizeof(char*));
    if (!d->context_items) return;
    d->context_capacity = count;
    d->context_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        d->context_items[i] = gui_strdup(items[i]);
        if (d->context_items[i]) d->context_count++;
    }
    d->context_open = 1;
    d->context_x = x;
    d->context_y = y;
    d->context_window = window_id;
    d->context_action = action;
    d->context_context = ctx;
}

void gui_context_menu_hide(GuiDesktop* d) {
    if (!d) return;
    d->context_open = 0;
    if (d->context_items) {
        for (uint32_t i = 0; i < d->context_count; i++) {
            if (d->context_items[i]) free(d->context_items[i]);
        }
        free(d->context_items);
        d->context_items = NULL;
    }
    d->context_count = d->context_capacity = 0;
}

void gui_start_menu_clear(GuiDesktop* d) {
    if (!d || !d->menu_items) return;
    for (uint32_t i = 0; i < d->menu_item_count; i++) {
        if (d->menu_items[i].label) free(d->menu_items[i].label);
        if (d->menu_items[i].action) free(d->menu_items[i].action);
        if (d->menu_items[i].args) free(d->menu_items[i].args);
    }
    free(d->menu_items);
    d->menu_items = NULL;
    d->menu_item_count = d->menu_item_capacity = 0;
}

int gui_start_menu_load(GuiDesktop* d) {
    if (!d) return -1;
    gui_start_menu_clear(d);
    for (uint32_t i = 0; i < 64; i++) {
        char key[32];
        const char* prefix = "USER/startmenu/";
        uint32_t k = 0;
        while (prefix[k]) { key[k] = prefix[k]; k++; }
        key[k++] = (char)('0' + (i / 10) % 10);
        key[k++] = (char)('0' + i % 10);
        key[k] = '\0';

        char value[256];
        int n = sys_reg_get_str(key, value, sizeof(value));
        if (n <= 0 || !value[0]) break;

        char* p1 = NULL; char* p2 = NULL;
        for (uint32_t j = 0; value[j]; j++) {
            if (value[j] == '|') {
                if (!p1) { p1 = &value[j]; *p1 = '\0'; }
                else { p2 = &value[j]; *p2 = '\0'; break; }
            }
        }
        const char* label = value;
        const char* action = p1 ? p1 + 1 : "";
        const char* args = p2 ? p2 + 1 : "";

        if (d->menu_item_count >= d->menu_item_capacity) {
            uint32_t newcap = d->menu_item_capacity ? d->menu_item_capacity * 2 : 8;
            GuiMenuItem* ni = (GuiMenuItem*)realloc(d->menu_items, newcap * sizeof(GuiMenuItem));
            if (!ni) return -1;
            for (uint32_t j = d->menu_item_capacity; j < newcap; j++) {
                ni[j].label = NULL; ni[j].action = NULL; ni[j].args = NULL;
            }
            d->menu_items = ni;
            d->menu_item_capacity = newcap;
        }
        GuiMenuItem* it = &d->menu_items[d->menu_item_count];
        it->label = gui_strdup(label);
        it->action = gui_strdup(action);
        it->args = args[0] ? gui_strdup(args) : NULL;
        if (it->label && it->action) d->menu_item_count++;
    }
    return (int)d->menu_item_count;
}

int gui_desktop_add_animation(GuiDesktop* d, int window_id,
                              int from_x, int from_y, int to_x, int to_y,
                              uint32_t start_ms, uint32_t duration_ms,
                              GuiAnimationKind kind) {
    if (!d || window_id < 0) return -1;
    if (d->animation_count >= d->animation_capacity) {
        uint32_t newcap = d->animation_capacity ? d->animation_capacity * 2 : 8;
        GuiAnimation* na = (GuiAnimation*)realloc(d->animations, newcap * sizeof(GuiAnimation));
        if (!na) return -1;
        d->animations = na;
        d->animation_capacity = newcap;
    }
    GuiAnimation* a = &d->animations[d->animation_count++];
    a->id = d->animation_count;
    a->window_id = window_id;
    a->from_x = from_x; a->from_y = from_y;
    a->to_x = to_x; a->to_y = to_y;
    a->start_ms = start_ms; a->duration_ms = duration_ms;
    a->kind = kind;
    a->active = 1;
    return (int)a->id;
}

void gui_desktop_update_animations(GuiDesktop* d, uint32_t now_ms) {
    if (!d) return;
    for (uint32_t i = 0; i < d->animation_count; i++) {
        GuiAnimation* a = &d->animations[i];
        if (!a->active) continue;
        uint32_t elapsed = now_ms - a->start_ms;
        GuiWindow* w = gui_window_find(d, a->window_id);
        if (!w) { a->active = 0; continue; }
        if (elapsed >= a->duration_ms) {
            a->active = 0;
            w->x = a->to_x; w->y = a->to_y;
            continue;
        }
        int progress = (int)((elapsed * 1000u) / a->duration_ms);
        w->x = a->from_x + ((a->to_x - a->from_x) * progress) / 1000;
        w->y = a->from_y + ((a->to_y - a->from_y) * progress) / 1000;
    }
}

static void fill_clamped(GuiDesktop* d, int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= (int)d->width || y >= (int)d->height) return;
    if (x + w > (int)d->width) w = (int)d->width - x;
    if (y + h > (int)d->height) h = (int)d->height - y;
    if (w <= 0 || h <= 0) return;
    render_fill_rect(x, y, w, h, color);
}

static void border_clamped(GuiDesktop* d, int x, int y, int w, int h, uint32_t color) {
    fill_clamped(d, x, y, w, 1, color);
    fill_clamped(d, x, y + h - 1, w, 1, color);
    fill_clamped(d, x, y, 1, h, color);
    fill_clamped(d, x + w - 1, y, 1, h, color);
}

static int rounded_inset(int row, int height) {
    static const uint8_t inset[WINDOW_RADIUS] = { 5, 3, 2, 1, 1, 0, 0 };
    if (row < WINDOW_RADIUS) return inset[row];
    int from_bottom = height - 1 - row;
    if (from_bottom < WINDOW_RADIUS) return inset[from_bottom];
    return 0;
}

static void rounded_fill(GuiDesktop* d, int x, int y, int w, int h, uint32_t color, int top_only) {
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++) {
        int inset = rounded_inset(row, h);
        if (top_only && row >= WINDOW_RADIUS) inset = 0;
        fill_clamped(d, x + inset, y + row, w - inset * 2, 1, color);
    }
}

static uint32_t text_len(const char* s) {
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static void draw_control(GuiDesktop* d, GuiControl* c, int x, int y) {
    if (!c->visible) return;
    uint32_t fg = c->fg_color ? c->fg_color : C_TEXT;
    uint32_t bg = c->bg_color ? c->bg_color : C_BG;
    uint32_t border = c->border_color ? c->border_color : C_BORDER;
    const char* text = c->text ? c->text : "";

    switch (c->type) {
        case GUI_LABEL:
            render_draw_text(x, y, fg, C_PANEL, text);
            break;
        case GUI_BUTTON: {
            uint32_t fill = c->focused ? (c->hover_color ? c->hover_color : C_BUTTON_H) : C_BUTTON;
            fill_clamped(d, x, y, c->width, c->height, fill);
            border_clamped(d, x, y, c->width, c->height, c->focused ? C_FOCUS : border);
            render_draw_text(x + 8, y + (c->height - 8) / 2, fg, fill, text);
            break;
        }
        case GUI_TEXTFIELD: {
            fill_clamped(d, x, y, c->width, c->height, bg);
            border_clamped(d, x, y, c->width, c->height, c->focused ? C_FOCUS : border);
            uint32_t length = text_len(text);
            uint32_t visible = c->width > 12 ? (uint32_t)(c->width - 12) / 8 : 0;
            uint32_t cursor = c->cursor < 0 ? 0 : (uint32_t)c->cursor;
            uint32_t first = cursor > visible ? cursor - visible : 0;
            if (first + visible > length && length > visible) first = length - visible;
            uint32_t count = length - first;
            if (count > visible) count = visible;
            char buf[GUI_TEXT_MAX];
            for (uint32_t i = 0; i < count; i++) buf[i] = text[first + i];
            buf[count] = '\0';
            render_draw_text(x + 6, y + (c->height - 8) / 2, fg, bg, buf);
            break;
        }
        case GUI_TEXTAREA: {
            fill_clamped(d, x, y, c->width, c->height, bg);
            border_clamped(d, x, y, c->width, c->height, c->focused ? C_FOCUS : border);
            int visible_rows = (c->height - 8) / 8;
            int total_rows = 1;
            int column = 0;
            for (uint32_t i = 0; text[i]; i++) {
                if (text[i] == '\r') continue;
                if (text[i] == '\n' || column >= (c->width - 12) / 8) {
                    total_rows++;
                    column = 0;
                    if (text[i] == '\n') continue;
                }
                column++;
            }
            int first_row = c->scroll ? total_rows - visible_rows : 0;
            if (first_row < 0) first_row = 0;
            int row = 0;
            column = 0;
            for (uint32_t i = 0; text[i] && row - first_row < visible_rows; i++) {
                if (text[i] == '\r') continue;
                if (text[i] == '\n' || column >= (c->width - 12) / 8) {
                    row++;
                    column = 0;
                    if (text[i] == '\n') continue;
                }
                if (row >= first_row) {
                    render_draw_text(x + 6 + column * 8, y + 4 + (row - first_row) * 8,
                                     fg, bg, (char[2]){ text[i], '\0' });
                }
                column++;
            }
            break;
        }
        case GUI_CANVAS:
            fill_clamped(d, x, y, c->width, c->height, bg);
            border_clamped(d, x, y, c->width, c->height, border);
            if (c->canvas_action) c->canvas_action(c->canvas_context, x + 1, y + 1,
                                                    c->width - 2, c->height - 2);
            break;
        case GUI_PANEL:
            fill_clamped(d, x, y, c->width, c->height, bg);
            border_clamped(d, x, y, c->width, c->height, border);
            break;
        case GUI_LIST: {
            fill_clamped(d, x, y, c->width, c->height, bg);
            border_clamped(d, x, y, c->width, c->height, border);
            int visible_rows = (c->height - 4) / 16;
            if (visible_rows < 1) visible_rows = 1;
            int track_height = c->height - 4;
            int content_width = c->width - 14;
            int max_scroll = (int)c->item_count - visible_rows;
            if (max_scroll < 0) max_scroll = 0;
            int scroll = c->scroll;
            if (scroll < 0) scroll = 0;
            if (scroll > max_scroll) scroll = max_scroll;
            for (int row = 0; row < visible_rows; row++) {
                uint32_t item = (uint32_t)(scroll + row);
                if (item >= c->item_count) break;
                int row_y = y + 2 + row * 16;
                uint32_t row_bg = ((int)item == c->selected) ? C_TITLE_F : C_BG;
                fill_clamped(d, x + 2, row_y, content_width, 16, row_bg);
                render_draw_text(x + 6, row_y + 4, fg, row_bg,
                                 c->items[item] ? c->items[item] : "");
            }
            fill_clamped(d, x + c->width - 10, y + 2, 6, track_height, C_PANEL);
            if (c->item_count > (uint32_t)visible_rows) {
                int thumb_h = track_height * visible_rows / (int)c->item_count;
                if (thumb_h < 12) thumb_h = 12;
                int thumb_range = track_height - thumb_h;
                int thumb_y = y + 2 + (max_scroll ? thumb_range * scroll / max_scroll : 0);
                uint32_t accent = d->accent_color ? d->accent_color : C_TITLE_F;
                fill_clamped(d, x + c->width - 10, thumb_y, 6, thumb_h, accent);
            }
            break;
        }
    }
}

static void draw_window(GuiDesktop* d, GuiWindow* w) {
    if (!w->used || !w->visible || w->minimized) return;
    int focused = (d->focused_window == w->id);
    uint32_t title_color = focused ? C_TITLE_F : C_TITLE;
    uint32_t border_color = focused ? C_TITLE_F : C_BORDER;
    rounded_fill(d, w->x + 3, w->y + 3, w->width, w->height, C_BLACK, 0);
    rounded_fill(d, w->x, w->y, w->width, w->height, border_color, 0);
    rounded_fill(d, w->x + 1, w->y + 1, w->width - 2, w->height - 2, C_PANEL, 0);
    rounded_fill(d, w->x + 1, w->y + 1, w->width - 2, TITLE_H, title_color, 1);
    int buttons_x = w->x + w->width - 3 * TITLE_BUTTON - 4;
    render_draw_text(w->x + 8, w->y + 8, C_TEXT, title_color, w->title);
    fill_clamped(d, buttons_x, w->y + 3, TITLE_BUTTON - 1, TITLE_H - 6, C_MINIMIZE);
    fill_clamped(d, buttons_x + TITLE_BUTTON, w->y + 3, TITLE_BUTTON - 1, TITLE_H - 6, C_MAXIMIZE);
    fill_clamped(d, buttons_x + 2 * TITLE_BUTTON, w->y + 3, TITLE_BUTTON - 1, TITLE_H - 6, C_CLOSE);
    fill_clamped(d, buttons_x + 5, w->y + 14, 8, 2, C_WHITE);
    border_clamped(d, buttons_x + TITLE_BUTTON + 5, w->y + 6, 9, 9, C_WHITE);
    for (int i = 0; i < 8; i++) {
        fill_clamped(d, buttons_x + 2 * TITLE_BUTTON + 5 + i, w->y + 7 + i, 2, 2, C_WHITE);
        fill_clamped(d, buttons_x + 2 * TITLE_BUTTON + 11 - i, w->y + 7 + i, 2, 2, C_WHITE);
    }
    for (GuiControl* c = w->controls; c; c = c->next) {
        draw_control(d, c, w->x + c->x, w->y + TITLE_H + c->y);
    }
}

void gui_desktop_draw(GuiDesktop* d) {
    if (!d) return;
    fill_clamped(d, 0, 0, (int)d->width, (int)d->height - TASKBAR_H,
                 d->background_color ? d->background_color : C_DESK);
    for (int z = 1; z < d->next_z; z++) {
        for (GuiWindow* w = d->windows; w; w = w->next) {
            if (w->used && w->visible && !w->minimized && w->z == z) draw_window(d, w);
        }
    }
    fill_clamped(d, 0, (int)d->height - TASKBAR_H, (int)d->width, TASKBAR_H, C_BG);
    uint32_t accent = d->accent_color ? d->accent_color : C_TITLE_F;
    fill_clamped(d, 8, (int)d->height - 24, 72, 20, accent);
    render_draw_text(20, (int)d->height - 18, C_WHITE, accent, "LiveOS");
    int task_x = 88;
    for (GuiWindow* w = d->windows; w; w = w->next) {
        if (!w->used) continue;
        uint32_t tbg = (d->focused_window == w->id && !w->minimized) ? C_BUTTON_H : C_BUTTON;
        fill_clamped(d, task_x, (int)d->height - 24, 104, 20, tbg);
        render_draw_text(task_x + 5, (int)d->height - 18, C_TEXT, tbg, w->title);
        task_x += 108;
    }
    if (d->start_open) {
        int row_h = 18;
        int visible_rows = (int)d->menu_item_count;
        if (visible_rows > 8) visible_rows = 8;
        int menu_h = 28 + visible_rows * row_h + 6;
        int menu_y = (int)d->height - TASKBAR_H - menu_h;
        fill_clamped(d, 8, menu_y, 200, menu_h, C_PANEL);
        border_clamped(d, 8, menu_y, 200, menu_h, C_BORDER);
        render_draw_text(18, menu_y + 10, C_TEXT, C_PANEL, "Applications");
        for (int row = 0; row < visible_rows; row++) {
            uint32_t i = (uint32_t)(d->start_scroll + row);
            if (i >= d->menu_item_count) break;
            int row_y = menu_y + 28 + row * row_h;
            int hovered = d->mouse_x >= 14 && d->mouse_x < 202 &&
                          d->mouse_y >= row_y && d->mouse_y < row_y + row_h;
            uint32_t bg = hovered ? C_BUTTON_H : C_PANEL;
            if (hovered) fill_clamped(d, 14, row_y, 188, row_h, C_BUTTON_H);
            render_draw_text(22, row_y + 5, C_TEXT, bg,
                             d->menu_items[i].label ? d->menu_items[i].label : "?");
        }
    }
    if (d->context_open) {
        int w = 168;
        int h = (int)d->context_count * CONTEXT_ROW_H + 4;
        fill_clamped(d, d->context_x, d->context_y, w, h, C_PANEL);
        border_clamped(d, d->context_x, d->context_y, w, h, C_BORDER);
        for (uint32_t i = 0; i < d->context_count; i++) {
            int row_y = d->context_y + 2 + (int)i * CONTEXT_ROW_H;
            int hovered = d->mouse_x >= d->context_x && d->mouse_x < d->context_x + w &&
                          d->mouse_y >= row_y && d->mouse_y < row_y + CONTEXT_ROW_H;
            uint32_t bg = hovered ? C_BUTTON_H : C_PANEL;
            if (hovered) fill_clamped(d, d->context_x + 1, row_y, w - 2, CONTEXT_ROW_H, bg);
            render_draw_text(d->context_x + 8, row_y + 6, C_TEXT, bg,
                             d->context_items[i] ? d->context_items[i] : "");
        }
    }
}

void gui_desktop_draw_window(GuiDesktop* d, int window_id) {
    GuiWindow* w = gui_window_find(d, window_id);
    if (w) draw_window(d, w);
}

void gui_desktop_draw_excluding(GuiDesktop* d, int excluded_window) {
    GuiWindow* ex = gui_window_find(d, excluded_window);
    int vis = ex ? ex->visible : 0;
    if (ex) ex->visible = 0;
    gui_desktop_draw(d);
    if (ex) ex->visible = vis;
}

static int window_at(GuiDesktop* d, int x, int y) {
    int found = -1, top_z = -1;
    for (GuiWindow* w = d->windows; w; w = w->next) {
        if (!w->used || !w->visible || w->minimized) continue;
        if (x >= w->x && x < w->x + w->width &&
            y >= w->y && y < w->y + w->height && w->z > top_z) {
            found = w->id;
            top_z = w->z;
        }
    }
    return found;
}

static GuiControl* control_at(GuiWindow* w, int x, int y) {
    for (GuiControl* c = w->controls; c; c = c->next) {
        if (!c->visible) continue;
        int left = w->x + c->x;
        int top = w->y + TITLE_H + c->y;
        if (x >= left && x < left + c->width && y >= top && y < top + c->height) return c;
    }
    return NULL;
}

static void focus_window(GuiDesktop* d, int id) {
    GuiWindow* target = gui_window_find(d, id);
    if (!target || !target->used) return;
    if (d->focused_window >= 0 && d->focused_window != id) {
        GuiWindow* old = gui_window_find(d, d->focused_window);
        if (old && old->focus_action) old->focus_action(old->focus_context, 0);
    }
    d->focused_window = id;
    target->visible = 1;
    target->minimized = 0;
    target->z = d->next_z++;
    if (target->focus_action) target->focus_action(target->focus_context, 1);
}

static void maximize_window(GuiDesktop* d, GuiWindow* w) {
    if (!w->maximized) {
        w->normal_x = w->x; w->normal_y = w->y;
        w->normal_width = w->width; w->normal_height = w->height;
        w->x = 0; w->y = 0;
        w->width = (int)d->width; w->height = (int)d->height - TASKBAR_H;
        w->maximized = 1;
    } else {
        w->x = w->normal_x; w->y = w->normal_y;
        w->width = w->normal_width; w->height = w->normal_height;
        w->maximized = 0;
    }
}

void gui_desktop_handle_pointer(GuiDesktop* d, int x, int y, uint32_t buttons) {
    if (!d) return;
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x >= (int)d->width) x = (int)d->width - 1;
    if (y >= (int)d->height) y = (int)d->height - 1;
    d->mouse_x = x; d->mouse_y = y;

    int pressed = (buttons & 1u) && !(d->mouse_buttons & 1u);
    int right_pressed = (buttons & 2u) && !(d->mouse_buttons & 2u);
    int released = !(buttons & 1u) && (d->mouse_buttons & 1u);
    d->old_buttons = d->mouse_buttons;
    d->mouse_buttons = buttons;

    if (right_pressed) {
        int id = window_at(d, x, y);
        if (id >= 0) {
            GuiWindow* w = gui_window_find(d, id);
            focus_window(d, id);
            if (w->context_action) w->context_action(w->context_context, x, y);
            else {
                static const char* items[] = { "Minimize", "Maximize / Restore", "Close" };
                gui_context_menu_show(d, x, y, items, 3, id, NULL, NULL);
            }
        } else {
            gui_context_menu_hide(d);
        }
        return;
    }
    if (pressed) {
        if (d->context_open) {
            int item = (y - d->context_y - 2) / CONTEXT_ROW_H;
            int inside = x >= d->context_x && x < d->context_x + 168 &&
                         item >= 0 && (uint32_t)item < d->context_count;
            GuiMenuAction action = d->context_action;
            void* ctx = d->context_context;
            int cwin = d->context_window;
            gui_context_menu_hide(d);
            if (inside) {
                if (action) action(ctx, item);
                else {
                    GuiWindow* t = gui_window_find(d, cwin);
                    if (t) {
                        if (item == 0) { t->minimized = 1; t->visible = 0;
                            if (t->focus_action) t->focus_action(t->focus_context, 0);
                            if (d->focused_window == cwin) d->focused_window = -1; }
                        else if (item == 1) maximize_window(d, t);
                        else if (item == 2) gui_window_destroy(d, cwin);
                    }
                }
            }
            return;
        }
        int taskbar_y = (int)d->height - TASKBAR_H;
        if (y >= taskbar_y && x < 82) {
            d->start_open = !d->start_open;
            d->start_scroll = 0;
            return;
        }
        if (y >= taskbar_y) {
            if (x < 88) return;
            int item = (x - 88) / 108;
            int seen = 0;
            for (GuiWindow* w = d->windows; w; w = w->next) {
                if (!w->used) continue;
                if (seen++ == item) {
                    if (d->focused_window == w->id && !w->minimized) {
                        w->minimized = 1; w->visible = 0;
                        if (w->focus_action) w->focus_action(w->focus_context, 0);
                        d->focused_window = -1;
                    } else {
                        focus_window(d, w->id);
                    }
                    return;
                }
            }
            return;
        }
        if (d->start_open) {
            int row_h = 18;
            int visible_rows = (int)d->menu_item_count;
            if (visible_rows > 8) visible_rows = 8;
            int menu_h = 28 + visible_rows * row_h + 6;
            int menu_y = (int)d->height - TASKBAR_H - menu_h;
            int row = (y - menu_y - 28) / row_h;
            if (x >= 14 && x < 202 && row >= 0 && row < visible_rows) {
                uint32_t idx = (uint32_t)(d->start_scroll + row);
                d->start_open = 0;
                if (idx < d->menu_item_count && d->launch_action) {
                    d->launch_action(d->launch_context,
                                     d->menu_items[idx].action ? d->menu_items[idx].action : "",
                                     d->menu_items[idx].args ? d->menu_items[idx].args : "");
                }
                return;
            }
            d->start_open = 0;
        }
        int id = window_at(d, x, y);
        if (id < 0) {
            if (d->focused_window >= 0) {
                GuiWindow* old = gui_window_find(d, d->focused_window);
                if (old && old->focus_action) old->focus_action(old->focus_context, 0);
            }
            d->focused_window = -1;
            return;
        }
        focus_window(d, id);
        GuiWindow* w = gui_window_find(d, id);
        if (y < w->y + TITLE_H) {
            int bx = w->x + w->width - 3 * TITLE_BUTTON - 4;
            if (x >= bx + 2 * TITLE_BUTTON) { gui_window_destroy(d, id); return; }
            if (x >= bx + TITLE_BUTTON) { maximize_window(d, w); return; }
            if (x >= bx) {
                w->minimized = 1; w->visible = 0;
                if (w->focus_action) w->focus_action(w->focus_context, 0);
                d->focused_window = -1;
                return;
            }
            if (w->maximized) return;
            w->dragging = 1;
            w->drag_dx = x - w->x;
            w->drag_dy = y - w->y;
            return;
        }
        GuiControl* c = control_at(w, x, y);
        if (c) {
            for (GuiControl* it = w->controls; it; it = it->next) it->focused = 0;
            w->focused_control = c->id;
            c->focused = 1;
            if (c->type == GUI_LIST) {
                int local_y = y - (w->y + TITLE_H + c->y);
                int visible_rows = (c->height - 4) / 16;
                if (visible_rows < 1) visible_rows = 1;
                int row = (local_y - 2) / 16;
                int selected = c->scroll + row;
                if (row >= 0 && row < visible_rows && selected < (int)c->item_count) {
                    c->selected = selected;
                    if (w->list_action) w->list_action(w->list_context, c);
                }
            }
            if (w->owner_pid > 0) {
                UserWindowMessage ev = {0};
                ev.type = USER_WINDOW_MSG_INPUT;
                ev.window_id = w->id;
                ev.control_id = c->id;
                ev.input_type = USER_WINDOW_INPUT_POINTER;
                ev.x = x - w->x;
                ev.y = y - w->y - TITLE_H;
                ev.selected = c->selected;
                sys_window_reply(w->owner_pid, &ev);
            } else {
                if (c->type == GUI_BUTTON) {
                    if (c->action) c->action(c->action_context, c);
                    else if (w->action) w->action(w->action_context, c);
                }
            }
        }
    }
    if (released) {
        for (GuiWindow* w = d->windows; w; w = w->next) w->dragging = 0;
    }
    if (buttons & 1u) {
        for (GuiWindow* w = d->windows; w; w = w->next) {
            if (w->used && w->dragging) {
                w->x = x - w->drag_dx;
                w->y = y - w->drag_dy;
            }
        }
    }
}

void gui_desktop_handle_key(GuiDesktop* d, int key) {
    if (!d) return;
    if (d->key_action) d->key_action(d->key_context, key);
    if (key == 27) { d->start_open = 0; gui_context_menu_hide(d); return; }
    if (d->focused_window < 0) return;
    GuiWindow* w = gui_window_find(d, d->focused_window);
    if (!w) return;
    GuiControl* c = NULL;
    if (w->focused_control >= 0) c = gui_control_find(d, w->id, w->focused_control);
    if (!c) return;
    if (w->owner_pid > 0) {
        UserWindowMessage ev = {0};
        ev.type = USER_WINDOW_MSG_INPUT;
        ev.window_id = w->id;
        ev.control_id = c->id;
        ev.input_type = USER_WINDOW_INPUT_KEY;
        ev.input_key = key;
        ev.selected = c->selected;
        sys_window_reply(w->owner_pid, &ev);
        return;
    }
    if (key == '\t') {
        GuiControl* next = NULL;
        int after = 0;
        for (GuiControl* it = w->controls; it; it = it->next) {
            if (it->type == GUI_LABEL || it->type == GUI_PANEL) continue;
            if (after) { next = it; break; }
            if (it->id == c->id) after = 1;
        }
        if (!next) for (GuiControl* it = w->controls; it; it = it->next) {
            if (it->type == GUI_LABEL || it->type == GUI_PANEL) continue;
            next = it; break;
        }
        if (next) {
            for (GuiControl* it = w->controls; it; it = it->next) it->focused = 0;
            next->focused = 1;
            w->focused_control = next->id;
        }
        return;
    }
    if (key == '\n' && c->type == GUI_BUTTON) {
        if (c->action) c->action(c->action_context, c);
        else if (w->action) w->action(w->action_context, c);
        return;
    }
    if (c->type == GUI_TEXTFIELD || c->type == GUI_TEXTAREA) {
        char* text = c->text;
        uint32_t len = text ? text_len(text) : 0;
        if (key == '\b' && c->cursor > 0 && (uint32_t)c->cursor <= len) {
            uint32_t cur = (uint32_t)--c->cursor;
            for (uint32_t i = cur; i < len; i++) text[i] = text[i + 1];
        } else if (key >= 32 && key < 127) {
            if (!text) {
                text = (char*)malloc(2);
                if (!text) return;
                text[0] = (char)key; text[1] = '\0';
                c->text = text; c->text_capacity = 2; c->cursor = 1;
                return;
            }
            if (len + 2 > c->text_capacity) {
                uint32_t newcap = c->text_capacity ? c->text_capacity * 2 : 8;
                char* nt = (char*)realloc(text, newcap);
                if (!nt) return;
                c->text = nt; c->text_capacity = newcap;
                text = nt;
            }
            uint32_t cur = c->cursor < 0 ? 0 : (uint32_t)c->cursor;
            if (cur > len) cur = len;
            for (uint32_t i = len + 1; i > cur; i--) text[i] = text[i - 1];
            text[cur] = (char)key;
            c->cursor++;
        }
    }
}

int gui_desktop_poll(GuiDesktop* d) {
    if (!d) return -1;
    UserInputState input;
    if (sys_input_poll(&input) != 0) return -1;
    int changed = (input.pointer_buttons != d->mouse_buttons) || (input.key != 0);
    gui_desktop_handle_pointer(d, input.pointer_x, input.pointer_y, input.pointer_buttons);
    if (input.key) gui_desktop_handle_key(d, input.key);
    return changed;
}