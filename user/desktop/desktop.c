static void fonts_closed(void* context);
#include <stddef.h>
#include "../sys.h"
#include "gui.h"
#include "render.h"
#include "window_server.h"

#define COLOR_BACKGROUND 0x00151B26u
#define COLOR_MUTED 0x0096A8B5u
#define COLOR_ACCENT_DEFAULT 0x0000A7C4u
#define NOTE_MAX 900
#define REGISTRY_PATH_MAX 160
#define FILE_PATH_MAX 192
#define TERMINAL_TEXT_MAX 700
#define UI_SCRIPT_MAX 900
#define MENU_APP_COUNT 7

static GuiDesktop desktop;
static WindowServer window_server;
static char registry_path[REGISTRY_PATH_MAX] = "SYSTEM";
static char note_path[128] = "/NOTES.TXT";
static char font_path[128];
static int note_window = -1;
static int note_path_control = -1;
static int note_text_control = -1;
static int note_status_control = -1;
static int registry_window = -1;
static int registry_value_control = -1;
static int settings_window = -1;
static int font_window = -1;
static int font_list_control = -1;
static char font_entries[GUI_LIST_ITEMS][64];
static uint32_t font_entry_count;
static char files_path[FILE_PATH_MAX] = "/";
static char files_names[GUI_LIST_CAPACITY][64];
static uint8_t files_attrs[GUI_LIST_CAPACITY];
static uint32_t files_count;
static int files_window = -1;
static int files_path_control = -1;
static int files_list_control = -1;
static int files_status_control = -1;
static uint32_t last_file_click_index = 0xFFFFFFFFu;
static uint32_t last_file_click_ticks;
static char file_context_commands[GUI_CONTEXT_ITEMS][128];
static char file_context_labels[GUI_CONTEXT_ITEMS][64];
static uint32_t file_context_count;
static char context_target_path[FILE_PATH_MAX];
static int script_window = -1;
static int script_text_control = -1;
static int script_canvas_control = -1;
static int script_status_control = -1;
static char script_source[UI_SCRIPT_MAX];
static char script_path[FILE_PATH_MAX] = "/UI.SCR";
static int32_t script_result;
static int terminal_window = -1;
static int terminal_output_control = -1;
static char terminal_output[TERMINAL_TEXT_MAX];

static void notepad_action(void* context, GuiControl* control);
static void open_notes(void);
static int load_note(void);
static void open_registry(void);
static void open_settings(void);
static void open_fonts(void);
static void show_files(void);
static void files_context(void* context, int x, int y);
static void files_context_action(void* context, int item);
static void files_window_context_action(void* context, int item);
static void window_context(void* context, int x, int y);
static void window_context_action(void* context, int item);
static void register_window_context(int window_id);
static void script_closed(void* context);
static void window_context(void* context, int x, int y);
static void window_context_action(void* context, int item);
static void register_window_context(int window_id);

static void copy_text(char* destination, uint32_t capacity, const char* source) {
    uint32_t index = 0;
    while (source && source[index] && index + 1 < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int text_equal(const char* left, const char* right) {
    uint32_t index = 0;
    while (left[index] && right[index] && left[index] == right[index]) index++;
    return left[index] == right[index];
}

static int parse_u32(const char* text, uint32_t* value) {
    uint32_t result = 0;
    if (!text || !*text) return -1;
    uint32_t base = 10;
    uint32_t index = 0;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        index = 2;
        if (!text[index]) return -1;
    }
    for (; text[index]; index++) {
        uint32_t digit;
        if (text[index] >= '0' && text[index] <= '9') digit = (uint32_t)(text[index] - '0');
        else if (base == 16 && text[index] >= 'a' && text[index] <= 'f') digit = (uint32_t)(text[index] - 'a' + 10);
        else if (base == 16 && text[index] >= 'A' && text[index] <= 'F') digit = (uint32_t)(text[index] - 'A' + 10);
        else return -1;
        if (result > (0xFFFFFFFFu - digit) / base) return -1;
        result = result * base + digit;
    }
    *value = result;
    return 0;
}

static int parse_script_expression(const char* text, int32_t* result) {
    int32_t values[16];
    char operators[16];
    uint32_t value_count = 0;
    uint32_t operator_count = 0;
    const char* cursor = text;
    while (*cursor) {
        while (*cursor == ' ' || *cursor == '\t') cursor++;
        if (value_count >= 16) return -1;
        int negative = 0;
        if (*cursor == '-') { negative = 1; cursor++; }
        if (*cursor < '0' || *cursor > '9') return -1;
        int64_t number = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            number = number * 10 + (*cursor++ - '0');
            if (number > 0x7FFFFFFF) return -1;
        }
        values[value_count++] = negative ? -(int32_t)number : (int32_t)number;
        while (*cursor == ' ' || *cursor == '\t') cursor++;
        if (!*cursor) break;
        if (*cursor != '+' && *cursor != '-' && *cursor != '*' && *cursor != '/') return -1;
        operators[operator_count++] = *cursor++;
    }
    if (value_count != operator_count + 1) return -1;
    for (uint32_t i = 0; i < operator_count;) {
        if (operators[i] != '*' && operators[i] != '/') { i++; continue; }
        if (operators[i] == '/' && values[i + 1] == 0) return -1;
        int64_t value = operators[i] == '*' ? (int64_t)values[i] * values[i + 1]
                           : values[i] / values[i + 1];
        if (value > 0x7FFFFFFF || value < -0x80000000LL) return -1;
        values[i] = (int32_t)value;
        for (uint32_t j = i + 1; j + 1 < value_count; j++) values[j] = values[j + 1];
        for (uint32_t j = i; j + 1 < operator_count; j++) operators[j] = operators[j + 1];
        value_count--;
        operator_count--;
    }
    int64_t total = values[0];
    for (uint32_t i = 0; i < operator_count; i++) {
        total = operators[i] == '+' ? total + values[i + 1] : total - values[i + 1];
        if (total > 0x7FFFFFFF || total < -0x80000000LL) return -1;
    }
    *result = (int32_t)total;
    return 0;
}

static int script_attribute(const char* line, const char* name, int32_t* value) {
    char needle[16];
    uint32_t length = 0;
    while (name[length] && length + 2 < sizeof(needle)) { needle[length] = name[length]; length++; }
    needle[length++] = '=';
    needle[length] = '\0';
    const char* start = NULL;
    for (uint32_t i = 0; line[i]; i++) {
        uint32_t j = 0;
        while (needle[j] && line[i + j] == needle[j]) j++;
        if (!needle[j]) { start = line + i + length; break; }
    }
    if (!start) return -1;
    char number[48];
    uint32_t n = 0;
    while (start[n] && start[n] != ' ' && start[n] != '>' && n + 1 < sizeof(number)) { number[n] = start[n]; n++; }
    number[n] = '\0';
    if (number[0] == '{' && number[1] == 'p' && number[2] == 'y' && number[3] == ':') {
        if (n < 6 || number[n - 1] != '}') return -1;
        number[n - 1] = '\0';
        return parse_script_expression(number + 4, value);
    }
    uint32_t parsed;
    if (parse_u32(number, &parsed) != 0) return -1;
    *value = (int32_t)parsed;
    return 0;
}

static void script_preview(void* context, int x, int y, int width, int height) {
    (void)context;
    uint32_t line_start = 0;
    while (line_start < UI_SCRIPT_MAX && script_source[line_start]) {
        char line[160];
        uint32_t length = 0;
        while (script_source[line_start] && script_source[line_start] != '\n' && length + 1 < sizeof(line)) {
            line[length++] = script_source[line_start++];
        }
        if (script_source[line_start] == '\n') line_start++;
        line[length] = '\0';
        int32_t px, py, w, h, color;
        if (line[0] == '<' && line[1] == 'r' && script_attribute(line, "x", &px) == 0 &&
            script_attribute(line, "y", &py) == 0 && script_attribute(line, "w", &w) == 0 &&
            script_attribute(line, "h", &h) == 0 && script_attribute(line, "color", &color) == 0) {
            if (w > 0 && h > 0) render_fill_rect(x + px, y + py, w < width ? w : width,
                                                  h < height ? h : height, (uint32_t)color);
        } else if (line[0] == '<' && line[1] == 't' && script_attribute(line, "x", &px) == 0 &&
                   script_attribute(line, "y", &py) == 0 && script_attribute(line, "color", &color) == 0) {
            const char* close = line;
            while (*close && *close != '>') close++;
            if (*close == '>') render_draw_text(x + px, y + py, (uint32_t)color, 0x00121922u, close + 1);
        } else if (line[0] == '{' && line[1] == 'p' && line[2] == 'y' && line[3] == ':') {
            char* end = line + length;
            if (end > line && end[-1] == '}') end[-1] = '\0';
            if (parse_script_expression(line + 4, &script_result) == 0) {
                char text[48] = "Math result: ";
                uint32_t pos = 12;
                int32_t value = script_result;
                if (value < 0) { text[pos++] = '-'; value = -value; }
                char digits[12]; uint32_t count = 0;
                do { digits[count++] = (char)('0' + value % 10); value /= 10; } while (value && count < sizeof(digits));
                while (count) text[pos++] = digits[--count];
                text[pos] = '\0';
                render_draw_text(x + 4, y + height - 12, 0x00FFFFFFu, 0x00121922u, text);
            }
        }
    }
}

static void script_action(void* context, GuiControl* control) {
    (void)context;
    if (script_window < 0 || !desktop.windows[script_window].used) return;
    if (control->text[0] == 'N') {
        script_source[0] = '\0';
        desktop.windows[script_window].controls[script_text_control].text[0] = '\0';
        copy_text(desktop.windows[script_window].controls[script_status_control].text,
                  sizeof(desktop.windows[script_window].controls[script_status_control].text), "New document");
        return;
    }
    if (control->text[0] == 'O') {
        char path[FILE_PATH_MAX];
        copy_text(path, sizeof(path), desktop.windows[script_window].controls[0].text);
        int fd = sys_open(path, FS_READ);
        if (fd < 0) {
            copy_text(desktop.windows[script_window].controls[script_status_control].text,
                      sizeof(desktop.windows[script_window].controls[script_status_control].text), "Open failed");
            return;
        }
        int count = sys_read(fd, script_source, sizeof(script_source) - 1);
        sys_close(fd);
        if (count < 0) return;
        script_source[count] = '\0';
        copy_text(desktop.windows[script_window].controls[script_text_control].text,
                  sizeof(desktop.windows[script_window].controls[script_text_control].text), script_source);
        copy_text(script_path, sizeof(script_path), path);
        copy_text(desktop.windows[script_window].controls[script_status_control].text,
                  sizeof(desktop.windows[script_window].controls[script_status_control].text), "Opened");
        return;
    }
    if (control->text[0] == 'P' || control->text[0] == 'S' || control->text[0] == 'R') {
        copy_text(script_source, sizeof(script_source), desktop.windows[script_window].controls[script_text_control].text);
        if (control->text[0] == 'S') {
            copy_text(script_path, sizeof(script_path), desktop.windows[script_window].controls[0].text);
            int fd = sys_open(script_path, FS_WRITE | FS_CREATE | FS_TRUNCATE);
            if (fd < 0) {
                copy_text(desktop.windows[script_window].controls[script_status_control].text,
                          sizeof(desktop.windows[script_window].controls[script_status_control].text), "Save failed");
                return;
            }
            uint32_t length = 0;
            while (script_source[length] && length < sizeof(script_source) - 1) length++;
            int written = sys_write(fd, script_source, length);
            sys_close(fd);
            copy_text(desktop.windows[script_window].controls[script_status_control].text,
                      sizeof(desktop.windows[script_window].controls[script_status_control].text),
                      written == (int)length ? "Saved" : "Save failed");
            return;
        }
        copy_text(desktop.windows[script_window].controls[script_status_control].text,
                  sizeof(desktop.windows[script_window].controls[script_status_control].text),
                  control->text[0] == 'R' ? "Script rendered" : "Preview updated");
    }
}

static void open_script_studio(void) {
    if (!script_path[0]) copy_text(script_path, sizeof(script_path), "/UI.SCR");
    int id = gui_window_create(&desktop, "UI Script Studio", 72, 44, 840, 620, NULL, NULL);
    if (id < 0) return;
    register_window_context(id);
    script_window = id;
    gui_window_set_close_action(&desktop, id, script_closed, NULL);
    gui_control_add(&desktop, id, GUI_TEXTFIELD, 10, 5, 280, 28, script_path);
    gui_control_add(&desktop, id, GUI_BUTTON, 298, 5, 64, 28, "New");
    gui_control_add(&desktop, id, GUI_BUTTON, 368, 5, 68, 28, "Open");
    gui_control_add(&desktop, id, GUI_BUTTON, 442, 5, 68, 28, "Save");
    gui_control_add(&desktop, id, GUI_BUTTON, 516, 5, 76, 28, "Preview");
    gui_control_add(&desktop, id, GUI_BUTTON, 598, 5, 68, 28, "Run");
    int editor = gui_control_add(&desktop, id, GUI_TEXTAREA, 10, 40, 390, 478,
        "<rect x=12 y=12 w=140 h=72 color=0x00385070>\n<text x=20 y=24 color=0x00FFFFFF>Hello</text>\n{py: 6 * 7}");
    script_text_control = editor;
    script_canvas_control = gui_control_add(&desktop, id, GUI_CANVAS, 414, 40, 410, 478, "");
    gui_canvas_set_action(&desktop, id, script_canvas_control, script_preview, NULL);
    script_status_control = gui_control_add(&desktop, id, GUI_LABEL, 10, 526, 800, 16, "Restricted markup + integer math");
    desktop.windows[id].action = script_action;
    script_source[0] = '\0';
}

static int load_ui_script(const char* path) {
    int fd = sys_open(path, FS_READ);
    if (fd < 0) return -1;
    char source[UI_SCRIPT_MAX];
    int count = sys_read(fd, source, sizeof(source) - 1);
    sys_close(fd);
    if (count < 0) return -1;
    source[count] = '\0';
    copy_text(script_path, sizeof(script_path), path);
    open_script_studio();
    if (script_window < 0 || !desktop.windows[script_window].used) return -1;
    copy_text(desktop.windows[script_window].controls[script_text_control].text,
              sizeof(desktop.windows[script_window].controls[script_text_control].text), source);
    copy_text(script_source, sizeof(script_source), source);
    return 0;
}

static void terminal_focus(void* context, int focused) {
    (void)context;
    sys_tty_focus(focused);
}

static void terminal_closed(void* context) {
    (void)context;
    sys_tty_reset();
    terminal_output[0] = '\0';
    terminal_window = -1;
    terminal_output_control = -1;
}

static void files_closed(void* context) {
    (void)context;
    files_path[0] = '/'; files_path[1] = '\0';
    files_count = 0;
    files_window = files_path_control = files_list_control = files_status_control = -1;
    file_context_count = 0;
    context_target_path[0] = '\0';
    last_file_click_index = 0xFFFFFFFFu;
    last_file_click_ticks = 0;
    for (uint32_t i = 0; i < GUI_LIST_CAPACITY; i++) files_names[i][0] = '\0';
    for (uint32_t i = 0; i < GUI_CONTEXT_ITEMS; i++) {
        file_context_commands[i][0] = '\0';
        file_context_labels[i][0] = '\0';
    }
}

static void notes_closed(void* context) {
    (void)context;
    note_path[0] = '\0';
    note_window = note_path_control = note_text_control = note_status_control = -1;
}

static void script_closed(void* context) {
    (void)context;
    script_source[0] = '\0';
    script_path[0] = '\0';
    script_window = script_text_control = script_canvas_control = script_status_control = -1;
}

static void registry_closed(void* context) {
    (void)context;
    registry_path[0] = 'S'; registry_path[1] = 'Y'; registry_path[2] = 'S';
    registry_path[3] = 'T'; registry_path[4] = 'E'; registry_path[5] = 'M'; registry_path[6] = '\0';
    registry_window = registry_value_control = -1;
}

static void settings_closed(void* context) {
    (void)context;
    settings_window = -1;
}

static void fonts_closed(void* context) {
    (void)context;
    font_window = font_list_control = -1;
    font_entry_count = 0;
    font_path[0] = '\0';
    for (uint32_t i = 0; i < GUI_LIST_ITEMS; i++) font_entries[i][0] = '\0';
}

static void show_desktop_window(int* window_id, void (*open_window)(void)) {
    if (*window_id < 0 || *window_id >= GUI_MAX_WINDOWS || !desktop.windows[*window_id].used) {
        *window_id = -1;
        open_window();
        return;
    }

    GuiWindow* previous = desktop.focused_window >= 0 && desktop.focused_window < GUI_MAX_WINDOWS
        ? &desktop.windows[desktop.focused_window] : NULL;
    if (previous && previous->used && previous->focus_action) {
        previous->focus_action(previous->focus_context, 0);
    }
    GuiWindow* window = &desktop.windows[*window_id];
    window->visible = 1;
    window->minimized = 0;
    window->z = desktop.next_z++;
    desktop.focused_window = *window_id;
    if (window->focus_action) window->focus_action(window->focus_context, 1);
    window_server_request_redraw(&window_server);
}

static int terminal_refresh(void) {
    if (terminal_window < 0 || terminal_output_control < 0) return 0;
    char chunk[128];
    int count;
    int changed = 0;
    while ((count = sys_tty_read(chunk, sizeof(chunk))) > 0) {
        changed = 1;
        uint32_t current = 0;
        while (terminal_output[current] && current + 1 < sizeof(terminal_output)) current++;
        for (int i = 0; i < count; i++) {
            if (chunk[i] == '\b') {
                if (current) current--;
                continue;
            }
            if (current + 1 >= sizeof(terminal_output)) {
                for (uint32_t j = 1; j < current; j++) terminal_output[j - 1] = terminal_output[j];
                current--;
            }
            terminal_output[current++] = chunk[i];
        }
        terminal_output[current] = '\0';
    }
    copy_text(desktop.windows[terminal_window].controls[terminal_output_control].text,
              sizeof(desktop.windows[terminal_window].controls[terminal_output_control].text), terminal_output);
    return changed;
}

static void open_fonts(void);

static int join_path(char* output, uint32_t capacity, const char* directory, const char* name) {
    uint32_t length = 0;
    while (directory[length] && length + 1 < capacity) {
        output[length] = directory[length];
        length++;
    }
    if (length && output[length - 1] != '/' && length + 1 < capacity) output[length++] = '/';
    uint32_t index = 0;
    while (name[index] && length + 1 < capacity) output[length++] = name[index++];
    output[length] = '\0';
    return name[index] ? -1 : 0;
}

static int normalize_files_path(const char* input, char* output, uint32_t capacity) {
    char joined[FILE_PATH_MAX * 2];
    uint32_t output_length = 1;
    uint32_t segment_starts[64];
    uint32_t depth = 0;
    if (!input || !input[0]) return -1;
    if (input[0] == '/') {
        copy_text(joined, sizeof(joined), input);
    } else {
        if (join_path(joined, sizeof(joined), files_path, input) != 0) return -1;
    }
    if (capacity < 2) return -1;
    output[0] = '/';
    output[1] = '\0';
    uint32_t cursor = 0;
    while (joined[cursor]) {
        while (joined[cursor] == '/') cursor++;
        uint32_t start = cursor;
        while (joined[cursor] && joined[cursor] != '/') cursor++;
        uint32_t part_length = cursor - start;
        if (!part_length) break;
        if (part_length == 1 && joined[start] == '.') continue;
        if (part_length == 2 && joined[start] == '.' && joined[start + 1] == '.') {
            if (depth) {
                output_length = segment_starts[--depth];
                output[output_length] = '\0';
            }
            continue;
        }
        uint32_t separator = output_length > 1 ? 1 : 0;
        if (depth >= sizeof(segment_starts) / sizeof(segment_starts[0]) ||
            output_length + separator + part_length + 1 > capacity) return -1;
        segment_starts[depth++] = output_length;
        if (separator) output[output_length++] = '/';
        for (uint32_t i = 0; i < part_length; i++) output[output_length++] = joined[start + i];
        output[output_length] = '\0';
    }
    return 0;
}

static void files_status(const char* message) {
    if (files_window >= 0 && files_status_control >= 0) {
        copy_text(desktop.windows[files_window].controls[files_status_control].text,
                  sizeof(desktop.windows[files_window].controls[files_status_control].text), message);
    }
}

static void files_refresh(void) {
    if (files_window < 0) return;
    GuiWindow* window = &desktop.windows[files_window];
    GuiControl* list = &window->controls[files_list_control];
    copy_text(window->controls[files_path_control].text,
              sizeof(window->controls[files_path_control].text), files_path);
    list->item_count = 0;
    list->selected = -1;
    list->scroll = 0;
    files_count = 0;
    for (uint32_t i = 0; i < GUI_LIST_CAPACITY; i++) {
        files_names[i][0] = '\0';
        files_attrs[i] = 0;
        list->items[i][0] = '\0';
    }
    int fd = sys_opendir(files_path);
    if (fd < 0) { files_status("Cannot open directory"); return; }
    for (uint32_t i = 0; i < GUI_LIST_CAPACITY; i++) {
        DirEntry entry;
        if (sys_readdir(fd, i, &entry) != 0) break;
        copy_text(files_names[i], sizeof(files_names[i]), entry.name);
        files_attrs[i] = entry.attr;
        char display[GUI_LIST_TEXT_MAX];
        display[0] = (entry.attr & FS_ATTR_DIR) ? '[' : ' ';
        display[1] = (entry.attr & FS_ATTR_DIR) ? 'D' : ' ';
        display[2] = (entry.attr & FS_ATTR_DIR) ? ']' : ' ';
        display[3] = ' ';
        copy_text(display + 4, sizeof(display) - 4, entry.name);
        gui_list_set_item(&desktop, files_window, files_list_control, i, display);
        files_count++;
    }
    sys_close(fd);
    files_status(files_count == GUI_LIST_CAPACITY ? "Showing first 256 entries" :
                 files_count ? "Enter/double click open" : "Empty directory");
}

static void open_file_path(const char* path) {
    const char* basename = path;
    for (uint32_t i = 0; path[i]; i++) if (path[i] == '/') basename = path + i + 1;
    const char* dot = NULL;
    for (uint32_t i = 0; basename[i]; i++) if (basename[i] == '.') dot = basename + i + 1;
    char extension[16] = {0};
    if (dot) {
        uint32_t i = 0;
        while (dot[i] && i + 1 < sizeof(extension)) {
            char c = dot[i];
            extension[i] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
            i++;
        }
    }
    if ((extension[0] == 'U' && extension[1] == 'I') ||
        (extension[0] == 'S' && extension[1] == 'C' && extension[2] == 'R')) {
        if (load_ui_script(path) != 0) files_status("Cannot open UI script");
        return;
    }
    char key[64] = "USER/quickopen/";
    uint32_t p = 15;
    for (uint32_t i = 0; extension[i] && p + 1 < sizeof(key); i++) key[p++] = extension[i];
    key[p] = '\0';
    char program[128] = {0};
    int found = extension[0] && sys_reg_get_str(key, program, sizeof(program)) > 0;
    if (!found && (extension[0] == 'T' && extension[1] == 'T' && extension[2] == 'F')) {
        open_fonts();
        return;
    }
    if (!found && (extension[0] == 'O' && extension[1] == 'T' && extension[2] == 'F')) {
        open_fonts();
        return;
    }
    if ((!found || program[0] == '@') && extension[0] == 'E' && extension[1] == 'L' && extension[2] == 'F') {
        int pid = sys_fork();
        if (pid == 0) {
            char* argv[] = { (char*)path, NULL };
            char* envp[] = { NULL };
            sys_exec(path, argv, envp);
            sys_exit(127);
        }
        return;
    }
    if (found) {
        if (program[0] != '/') {
            char absolute[160] = "/bin/";
            copy_text(absolute + 5, sizeof(absolute) - 5, program);
            copy_text(program, sizeof(program), absolute);
        }
        int pid = sys_fork();
        if (pid == 0) {
            char* argv[] = { program, (char*)path, NULL };
            char* envp[] = { NULL };
            sys_exec(program, argv, envp);
            sys_exit(127);
        }
        return;
    }
    note_window = -1;
    copy_text(note_path, sizeof(note_path), path);
    open_notes();
    load_note();
}

static int file_type_name(const char* extension, char* output, uint32_t capacity) {
    const char* type = "TEXT";
    if (!extension || !extension[0]) type = "FILE";
    else if (text_equal(extension, "PNG") || text_equal(extension, "ICO") || text_equal(extension, "JPG") || text_equal(extension, "BMP")) type = "IMAGE";
    else if (text_equal(extension, "MP4") || text_equal(extension, "AVI") || text_equal(extension, "MKV")) type = "VIDEO";
    else if (text_equal(extension, "MP3") || text_equal(extension, "WAV") || text_equal(extension, "OGG")) type = "AUDIO";
    else if (text_equal(extension, "ELF") || text_equal(extension, "APP")) type = "APPLICATION";
    copy_text(output, capacity, type);
    return 0;
}

static void files_add_registry_actions(const char* key) {
    if (file_context_count >= GUI_CONTEXT_ITEMS) return;
    for (uint32_t index = 0; index < GUI_CONTEXT_ITEMS; index++) {
        UserRegEntry entry;
        if (sys_reg_enum(key, index, &entry) != 0) break;
        if (entry.is_key || !entry.text[0] || file_context_count >= GUI_CONTEXT_ITEMS) continue;
        copy_text(file_context_commands[file_context_count], sizeof(file_context_commands[0]), entry.text);
        copy_text(file_context_labels[file_context_count], sizeof(file_context_labels[0]), entry.name);
        file_context_count++;
    }
}

static void files_context(void* context, int x, int y) {
    (void)context;
    if (files_window < 0 || !desktop.windows[files_window].used) return;
    if (x < desktop.windows[files_window].x + 10 ||
        x >= desktop.windows[files_window].x + 600 ||
        y < desktop.windows[files_window].y + 24 + 38 ||
        y >= desktop.windows[files_window].y + 24 + 396) {
        static const char* window_items[] = { "Minimize", "Maximize / Restore", "Close" };
        gui_context_menu_show(&desktop, x, y, window_items, 3, files_window,
                              files_window_context_action, NULL);
        return;
    }
    GuiControl* list = &desktop.windows[files_window].controls[files_list_control];
    int row = (y - (desktop.windows[files_window].y + 24 + 40)) / 16;
    int index = list->scroll + row;
    if (row < 0 || index < 0 || (uint32_t)index >= files_count || !files_names[index][0]) return;
    list->selected = index;
    char candidate[FILE_PATH_MAX];
    if (join_path(candidate, sizeof(candidate), files_path, files_names[index]) != 0 ||
        normalize_files_path(candidate, context_target_path, sizeof(context_target_path)) != 0) return;
    const char* basename = files_names[index];
    const char* dot = NULL;
    for (uint32_t i = 0; basename[i]; i++) if (basename[i] == '.') dot = basename + i + 1;
    char extension[16] = {0};
    if (dot) {
        for (uint32_t i = 0; dot[i] && i + 1 < sizeof(extension); i++) {
            char c = dot[i];
            extension[i] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
        }
    }
    char type[24];
    file_type_name(extension, type, sizeof(type));
    file_context_count = 0;
    char key[96] = "USER/context/ext/";
    copy_text(key + 17, sizeof(key) - 17, extension);
    if (extension[0]) files_add_registry_actions(key);
    copy_text(key, sizeof(key), "USER/context/type/");
    copy_text(key + 18, sizeof(key) - 18, type);
    files_add_registry_actions(key);
    files_add_registry_actions("USER/context/global");
    if ((extension[0] == 'U' && extension[1] == 'I') ||
        (extension[0] == 'S' && extension[1] == 'C' && extension[2] == 'R')) {
        if (file_context_count < GUI_CONTEXT_ITEMS) {
            copy_text(file_context_commands[file_context_count], sizeof(file_context_commands[0]), "@run-ui");
            copy_text(file_context_labels[file_context_count++], sizeof(file_context_labels[0]), "Run in UI Studio");
        }
    }
    if (file_context_count < GUI_CONTEXT_ITEMS - 1) {
        copy_text(file_context_commands[file_context_count], sizeof(file_context_commands[0]), "@open");
        copy_text(file_context_labels[file_context_count++], sizeof(file_context_labels[0]), "Open");
    }
    if (!(files_attrs[row] & FS_ATTR_DIR) && file_context_count < GUI_CONTEXT_ITEMS) {
        copy_text(file_context_commands[file_context_count], sizeof(file_context_commands[0]), "@delete");
        copy_text(file_context_labels[file_context_count++], sizeof(file_context_labels[0]), "Delete");
    }
    const char* labels[GUI_CONTEXT_ITEMS];
    for (uint32_t i = 0; i < file_context_count; i++) labels[i] = file_context_labels[i];
    gui_context_menu_show(&desktop, x, y, labels, file_context_count, files_window,
                         files_context_action, NULL);
}

static void files_window_context_action(void* context, int item) {
    (void)context;
    if (files_window < 0 || !desktop.windows[files_window].used) return;
    GuiWindow* window = &desktop.windows[files_window];
    if (item == 0) {
        window->minimized = 1;
        window->visible = 0;
        desktop.focused_window = -1;
    } else if (item == 1) {
        window->maximized = !window->maximized;
        if (window->maximized) {
            window->normal_x = window->x;
            window->normal_y = window->y;
            window->normal_width = window->width;
            window->normal_height = window->height;
            window->x = 0;
            window->y = 0;
            window->width = (int)desktop.width;
            window->height = (int)desktop.height - 28;
        } else {
            window->x = window->normal_x;
            window->y = window->normal_y;
            window->width = window->normal_width;
            window->height = window->normal_height;
        }
    } else if (item == 2) gui_window_destroy(&desktop, files_window);
}

static void files_context_action(void* context, int item) {
    (void)context;
    if (item < 0 || (uint32_t)item >= file_context_count) return;
    if (files_window < 0 || !desktop.windows[files_window].used) return;
    const char* command = file_context_commands[item];
    if (text_equal(command, "@run-ui")) {
        if (load_ui_script(context_target_path) != 0) files_status("Cannot run UI script");
    } else if (text_equal(command, "@open")) {
        int selected = desktop.windows[files_window].controls[files_list_control].selected;
        if (selected < 0 || (uint32_t)selected >= files_count) return;
        if (files_attrs[selected] & FS_ATTR_DIR) {
            copy_text(files_path, sizeof(files_path), context_target_path);
            files_refresh();
        } else open_file_path(context_target_path);
    } else if (text_equal(command, "@delete")) {
        int selected = desktop.windows[files_window].controls[files_list_control].selected;
        int result = (files_attrs[selected] & FS_ATTR_DIR) ? sys_rmdir(context_target_path) : sys_unlink(context_target_path);
        files_status(result == 0 ? "Deleted" : "Delete failed");
        files_refresh();
    } else if (command[0]) {
        char program[128];
        copy_text(program, sizeof(program), command);
        if (program[0] != '/') {
            char absolute[160] = "/bin/";
            copy_text(absolute + 5, sizeof(absolute) - 5, program);
            copy_text(program, sizeof(program), absolute);
        }
        int pid = sys_fork();
        if (pid == 0) {
            char* argv[] = { program, context_target_path, NULL };
            char* envp[] = { NULL };
            sys_exec(program, argv, envp);
            sys_exit(127);
        }
    }
}

static void files_activate(void* context, GuiControl* control) {
    (void)context;
    if (control->selected < 0 || (uint32_t)control->selected >= files_count) return;
    uint32_t index = (uint32_t)control->selected;
    char path[FILE_PATH_MAX];
    char candidate[FILE_PATH_MAX];
    if (join_path(candidate, sizeof(candidate), files_path, files_names[index]) != 0 ||
        normalize_files_path(candidate, path, sizeof(path)) != 0) return;
    if (files_attrs[index] & FS_ATTR_DIR) {
        last_file_click_index = 0xFFFFFFFFu;
        copy_text(files_path, sizeof(files_path), path);
        files_refresh();
        return;
    }
    open_file_path(path);
}

static void files_list_click(void* context, GuiControl* control) {
    (void)context;
    uint32_t now = sys_ticks();
    if (control->selected >= 0 && (uint32_t)control->selected == last_file_click_index &&
        now - last_file_click_ticks <= 50) {
        files_activate(NULL, control);
        last_file_click_index = 0xFFFFFFFFu;
        last_file_click_ticks = 0;
        return;
    }
    last_file_click_index = control->selected < 0 ? 0xFFFFFFFFu : (uint32_t)control->selected;
    last_file_click_ticks = now;
}

static void files_action(void* context, GuiControl* control) {
    (void)context;
    if (control->text[0] == 'O') {
        if (normalize_files_path(desktop.windows[files_window].controls[files_path_control].text,
                                 files_path, sizeof(files_path)) != 0) {
            files_status("Invalid path");
            return;
        }
        files_refresh();
    } else if (control->text[0] == 'U') {
        uint32_t length = 0;
        while (files_path[length]) length++;
        while (length > 1 && files_path[length - 1] != '/') length--;
        if (length > 1) files_path[length - 1] = '\0';
        else copy_text(files_path, sizeof(files_path), "/");
        files_refresh();
    } else if (control->text[0] == 'N') {
        char folder[FILE_PATH_MAX];
        if (join_path(folder, sizeof(folder), files_path, "NewFolder") == 0 && sys_mkdir(folder) == 0) files_refresh();
        else files_status("Create folder failed");
    } else if (control->text[0] == 'D') {
        GuiControl* list = &desktop.windows[files_window].controls[files_list_control];
        int selected = list->selected;
        if (selected < 0 || (uint32_t)selected >= files_count) return;
        char path[FILE_PATH_MAX];
        if (join_path(path, sizeof(path), files_path, files_names[selected]) != 0) return;
        int result = (files_attrs[selected] & FS_ATTR_DIR) ? sys_rmdir(path) : sys_unlink(path);
        files_status(result == 0 ? "Deleted" : "Delete failed");
        files_refresh();
    } else if (control->text[0] == 'S') {
        sys_tty_focus(0);
    }
}

static void files_key_action(void* context, int key) {
    (void)context;
    if (files_window < 0 || !desktop.windows[files_window].used || desktop.focused_window != files_window ||
        key != '\n' || desktop.windows[files_window].focused_control != files_path_control) return;
    GuiControl* path_control = &desktop.windows[files_window].controls[files_path_control];
    if (normalize_files_path(path_control->text, files_path, sizeof(files_path)) != 0) {
        files_status("Invalid path");
        return;
    }
    files_refresh();
}

static void open_terminal(void) {
    int id = gui_window_create(&desktop, "Terminal", 24, 44, 540, 360, NULL, NULL);
    if (id < 0) return;
    register_window_context(id);
    terminal_window = id;
    terminal_output[0] = '\0';
    sys_tty_reset();
    sys_tty_focus(1);
    terminal_output_control = gui_control_add(&desktop, id, GUI_TEXTAREA, 10, 8, 510, 310, terminal_output);
    if (terminal_output_control >= 0) desktop.windows[id].controls[terminal_output_control].scroll = 1;
    gui_control_add(&desktop, id, GUI_LABEL, 12, 326, 510, 16, "Type commands while this window is focused");
    gui_window_set_focus_action(&desktop, id, terminal_focus, NULL);
    gui_window_set_close_action(&desktop, id, terminal_closed, NULL);
    terminal_refresh();
}

static void open_files(void) {
    int id = gui_window_create(&desktop, "Files", 180, 110, 620, 470, NULL, NULL);
    if (id < 0) return;
    register_window_context(id);
    files_window = id;
    gui_window_set_close_action(&desktop, id, files_closed, NULL);
    gui_control_add(&desktop, id, GUI_LABEL, 10, 8, 40, 16, "Path");
    files_path_control = gui_control_add(&desktop, id, GUI_TEXTFIELD, 50, 4, 310, 24, files_path);
    gui_control_add(&desktop, id, GUI_BUTTON, 368, 3, 64, 26, "Open");
    gui_control_add(&desktop, id, GUI_BUTTON, 438, 3, 64, 26, "Up");
    gui_control_add(&desktop, id, GUI_BUTTON, 508, 3, 72, 26, "New Dir");
    files_list_control = gui_control_add(&desktop, id, GUI_LIST, 10, 38, 590, 358, "");
    files_status_control = gui_control_add(&desktop, id, GUI_LABEL, 12, 404, 580, 16, "");
    gui_window_set_list_action(&desktop, id, files_list_click, NULL);
    gui_window_set_list_activate_action(&desktop, id, files_activate, NULL);
    gui_window_set_context_action(&desktop, id, files_context, NULL);
    desktop.windows[id].action = files_action;
    files_refresh();
}

static void show_files(void) {
    show_desktop_window(&files_window, open_files);
}

static void show_registry(void) {
    show_desktop_window(&registry_window, open_registry);
}

static void show_settings(void) {
    show_desktop_window(&settings_window, open_settings);
}

static void show_fonts(void) {
    show_desktop_window(&font_window, open_fonts);
}

static void open_notes(void) {
    int id = gui_window_create(&desktop, "Notepad", 300, 180, 610, 470, NULL, NULL);
    if (id < 0) return;
    register_window_context(id);
    note_window = id;
    gui_window_set_close_action(&desktop, id, notes_closed, NULL);
    gui_control_add(&desktop, id, GUI_LABEL, 12, 8, 60, 16, "File");
    note_path_control = gui_control_add(&desktop, id, GUI_TEXTFIELD, 54, 4, 300, 24, note_path);
    gui_control_add(&desktop, id, GUI_BUTTON, 366, 3, 74, 26, "Load");
    gui_control_add(&desktop, id, GUI_BUTTON, 448, 3, 74, 26, "Save");
    gui_control_add(&desktop, id, GUI_TEXTAREA, 10, 36, 578, 372, "");
    note_text_control = (int)desktop.windows[id].control_count - 1;
    note_status_control = gui_control_add(&desktop, id, GUI_LABEL, 12, 414, 560, 16, "Ready");
    desktop.windows[id].action = notepad_action;
}

static int load_note(void) {
    if (note_window < 0 || note_path_control < 0 || note_text_control < 0) return -1;
    GuiWindow* window = &desktop.windows[note_window];
    copy_text(note_path, sizeof(note_path), window->controls[note_path_control].text);
    int fd = sys_open(note_path, FS_READ);
    if (fd < 0) {
        copy_text(window->controls[note_status_control].text,
                  sizeof(window->controls[note_status_control].text), "Open failed");
        return -1;
    }
    GuiControl* text = &window->controls[note_text_control];
    int count = sys_read(fd, text->text, NOTE_MAX);
    sys_close(fd);
    if (count < 0) {
        copy_text(window->controls[note_status_control].text,
                  sizeof(window->controls[note_status_control].text), "Read failed");
        return -1;
    }
    text->text[count] = '\0';
    copy_text(window->controls[note_status_control].text,
              sizeof(window->controls[note_status_control].text), "Loaded");
    return 0;
}

static int save_note(void) {
    if (note_window < 0 || note_path_control < 0 || note_text_control < 0) return -1;
    GuiWindow* window = &desktop.windows[note_window];
    copy_text(note_path, sizeof(note_path), window->controls[note_path_control].text);
    int fd = sys_open(note_path, FS_WRITE | FS_CREATE | FS_TRUNCATE);
    if (fd < 0) {
        copy_text(window->controls[note_status_control].text,
                  sizeof(window->controls[note_status_control].text), "Save failed");
        return -1;
    }
    const char* text = window->controls[note_text_control].text;
    uint32_t length = 0;
    while (text[length] && length < NOTE_MAX) length++;
    int written = sys_write(fd, text, length);
    sys_close(fd);
    copy_text(window->controls[note_status_control].text,
              sizeof(window->controls[note_status_control].text),
              written == (int)length ? "Saved" : "Save failed");
    return written == (int)length ? 0 : -1;
}

static void notepad_action(void* context, GuiControl* control) {
    (void)context;
    if (control->text[0] == 'L') load_note();
    if (control->text[0] == 'S') save_note();
}

static void registry_refresh(void) {
    if (registry_window < 0) return;
    int list_id = registry_value_control;
    for (uint32_t i = 0; i < GUI_LIST_ITEMS; i++) {
        gui_list_set_item(&desktop, registry_window, list_id, i, "");
    }
    for (uint32_t i = 0; i < GUI_LIST_ITEMS; i++) {
        UserRegEntry entry;
        if (sys_reg_enum(registry_path, i, &entry) != 0) break;
        char line[GUI_TEXT_MAX];
        uint32_t p = 0;
        const char* prefix = entry.is_key ? "[+] " : "     ";
        while (*prefix) line[p++] = *prefix++;
        for (uint32_t j = 0; entry.name[j] && p + 1 < sizeof(line); j++) line[p++] = entry.name[j];
        if (!entry.is_key && p + 3 < sizeof(line)) {
            line[p++] = ' '; line[p++] = '='; line[p++] = ' ';
            for (uint32_t j = 0; entry.text[j] && p + 1 < sizeof(line); j++) line[p++] = entry.text[j];
        }
        line[p] = '\0';
        gui_list_set_item(&desktop, registry_window, list_id, i, line);
    }
}

static void registry_select(void* context, GuiControl* control) {
    (void)context;
    int selected = control->selected;
    if (selected < 0) return;
    UserRegEntry entry;
    if (sys_reg_enum(registry_path, (uint32_t)selected, &entry) != 0) return;
    if (!entry.is_key) {
        copy_text(desktop.windows[registry_window].controls[1].text,
                  sizeof(desktop.windows[registry_window].controls[1].text), entry.text);
        return;
    }
    uint32_t length = 0;
    while (registry_path[length] && length + 1 < sizeof(registry_path)) length++;
    if (length + 1 + 32 >= sizeof(registry_path)) return;
    registry_path[length++] = '/';
    copy_text(registry_path + length, sizeof(registry_path) - length, entry.name);
    copy_text(desktop.windows[registry_window].controls[0].text,
              sizeof(desktop.windows[registry_window].controls[0].text), registry_path);
    registry_refresh();
}

static void registry_up(void) {
    uint32_t length = 0;
    while (registry_path[length]) length++;
    while (length && registry_path[length - 1] != '/') length--;
    if (length && length > 1) registry_path[length - 1] = '\0';
    else copy_text(registry_path, sizeof(registry_path), "SYSTEM");
    registry_refresh();
}

static void registry_action(void* context, GuiControl* control) {
    (void)context;
    if (control == &desktop.windows[registry_window].controls[0]) {
        copy_text(registry_path, sizeof(registry_path), control->text);
        registry_refresh();
        return;
    }
    if (control->text[0] == 'U') {
        registry_up();
        copy_text(desktop.windows[registry_window].controls[0].text,
                  sizeof(desktop.windows[registry_window].controls[0].text), registry_path);
        return;
    }
    UserRegEntry entry;
    int selected = desktop.windows[registry_window].controls[registry_value_control].selected;
    if (selected < 0 || sys_reg_enum(registry_path, (uint32_t)selected, &entry) != 0 || entry.is_key) return;
    char full_path[256];
    uint32_t p = 0;
    for (; registry_path[p] && p + 1 < sizeof(full_path); p++) full_path[p] = registry_path[p];
    if (p && full_path[p - 1] != '/') full_path[p++] = '/';
    for (uint32_t i = 0; entry.name[i] && p + 1 < sizeof(full_path); i++) full_path[p++] = entry.name[i];
    full_path[p] = '\0';
    GuiControl* edit = &desktop.windows[registry_window].controls[1];
    if (entry.type == 2) sys_reg_set_str(full_path, edit->text);
    else {
        uint32_t value;
        if (parse_u32(edit->text, &value) == 0) sys_reg_set_u32(full_path, value);
    }
    registry_refresh();
}

static void open_registry(void) {
    int id = gui_window_create(&desktop, "Registry Editor", 80, 72, 720, 520, NULL, NULL);
    if (id < 0) return;
    register_window_context(id);
    registry_window = id;
    gui_window_set_close_action(&desktop, id, registry_closed, NULL);
    gui_control_add(&desktop, id, GUI_TEXTFIELD, 12, 4, 220, 24, registry_path);
    gui_control_add(&desktop, id, GUI_TEXTFIELD, 244, 4, 300, 24, "");
    gui_control_add(&desktop, id, GUI_BUTTON, 558, 3, 64, 26, "Up");
    gui_control_add(&desktop, id, GUI_BUTTON, 630, 3, 74, 26, "Apply");
    registry_value_control = gui_control_add(&desktop, id, GUI_LIST, 12, 38, 690, 426, "");
    desktop.windows[id].action = registry_action;
    gui_window_set_list_action(&desktop, id, registry_select, NULL);
    registry_refresh();
}

static void load_fonts(void) {
    if (font_window < 0) return;
    int fd = sys_opendir("/usr/fonts");
    if (fd < 0) {
        gui_list_set_item(&desktop, font_window, font_list_control, 0, "No /usr/fonts directory");
        return;
    }
    uint32_t row = 0;
    font_entry_count = 0;
    for (uint32_t index = 0; index < 64 && row < GUI_LIST_ITEMS; index++) {
        DirEntry entry;
        if (sys_readdir(fd, index, &entry) != 0) break;
        const char* name = entry.name;
        uint32_t length = 0;
        while (name[length]) length++;
        if (length < 4) continue;
        const char* extension = name + length - 4;
        int ttf = (extension[0] == '.' && (extension[1] == 't' || extension[1] == 'T') &&
               (extension[2] == 't' || extension[2] == 'T') &&
               (extension[3] == 'f' || extension[3] == 'F'));
        int otf = (extension[0] == '.' && (extension[1] == 'o' || extension[1] == 'O') &&
               (extension[2] == 't' || extension[2] == 'T') &&
               (extension[3] == 'f' || extension[3] == 'F'));
        int supported = ttf || otf;
        if (supported && row < GUI_LIST_ITEMS) {
            copy_text(font_entries[row], sizeof(font_entries[row]), name);
            gui_list_set_item(&desktop, font_window, font_list_control, row, name);
            row++;
        }
    }
    font_entry_count = row;
    sys_close(fd);
}

static void font_select(void* context, GuiControl* control) {
    (void)context;
    int selected = control->selected;
    if (selected < 0 || (uint32_t)selected >= font_entry_count) return;
    int p = 0;
    const char* prefix = "/usr/fonts/";
    while (*prefix) font_path[p++] = *prefix++;
    copy_text(font_path + p, sizeof(font_path) - (uint32_t)p, font_entries[selected]);
}

static void font_apply(void* context, GuiControl* control) {
    (void)context;
    (void)control;
    GuiControl* size_field = &desktop.windows[font_window].controls[3];
    uint32_t point_size = 12;
    if (parse_u32(size_field->text, &point_size) != 0 || point_size < 6 || point_size > 36) return;
    uint32_t scale = (point_size + 5) / 6;
    if (scale > 6) scale = 6;
    if (sys_reg_set_u32("USER/fonts/scale", scale) != 0) return;
    sys_reg_set_u32("USER/fonts/size_pt", point_size);
    render_set_font_scale(scale);
    if (font_path[0]) sys_reg_set_str("USER/fonts/default", font_path);
}

static void open_fonts(void) {
    sys_mkdir("/usr");
    sys_mkdir("/usr/fonts");
    int id = gui_window_create(&desktop, "Font Settings", 180, 90, 560, 450, NULL, NULL);
    if (id < 0) return;
    register_window_context(id);
    font_window = id;
    gui_window_set_close_action(&desktop, id, fonts_closed, NULL);
    sys_reg_get_str("USER/fonts/default", font_path, sizeof(font_path));
    gui_control_add(&desktop, id, GUI_LABEL, 12, 8, 520, 16, "Select .ttf/.otf from /usr/fonts; bitmap renderer remains active.");
    font_list_control = gui_control_add(&desktop, id, GUI_LIST, 12, 34, 520, 320, "");
    char size_text[16];
    uint32_t point_size = sys_reg_get_u32("USER/fonts/size_pt");
    if (point_size < 6 || point_size > 36) point_size = 12;
    size_text[0] = (char)('0' + point_size / 10);
    size_text[1] = (char)('0' + point_size % 10);
    size_text[2] = '\0';
    gui_control_add(&desktop, id, GUI_LABEL, 12, 366, 120, 16, "Font size (pt)");
    gui_control_add(&desktop, id, GUI_TEXTFIELD, 132, 360, 56, 26, size_text);
    gui_control_add(&desktop, id, GUI_BUTTON, 430, 364, 100, 28, "Apply");
    gui_window_set_list_action(&desktop, id, font_select, NULL);
    desktop.windows[id].action = font_apply;
    load_fonts();
}

static void settings_apply(void* context, GuiControl* control) {
    (void)control;
    int window_id = (int)(intptr_t)context;
    if (window_id < 0 || window_id >= GUI_MAX_WINDOWS || !desktop.windows[window_id].used) return;
    GuiWindow* window = &desktop.windows[window_id];
    if (control->text[0] == 'A') {
        uint32_t background;
        uint32_t accent;
        if (parse_u32(window->controls[1].text, &background) != 0 ||
            parse_u32(window->controls[3].text, &accent) != 0) return;
        sys_reg_set_u32("USER/desktop/background", background);
        sys_reg_set_u32("USER/desktop/accent", accent);
        gui_desktop_set_theme(&desktop, background, accent);
        return;
    }
    GuiControl* serial_setting = &window->controls[7];
    uint32_t serial_enabled;
    if (parse_u32(serial_setting->text, &serial_enabled) == 0) {
        serial_enabled = serial_enabled != 0;
        sys_reg_set_u32("SYSTEM/logging/serial_enabled", serial_enabled);
        sys_serial_debug((int)serial_enabled);
    }
    char quick_key[64];
    char extension[16];
    for (uint32_t pair = 0; pair < 3; pair++) {
        uint32_t extension_control = 9 + pair * 2;
        uint32_t program_control = extension_control + 1;
        if (extension_control >= window->control_count || program_control >= window->control_count) break;
        copy_text(extension, sizeof(extension), window->controls[extension_control].text);
        copy_text(quick_key, sizeof(quick_key), "USER/quickopen/");
        uint32_t key_length = 15;
        for (uint32_t i = 0; extension[i] && key_length + 1 < sizeof(quick_key); i++) {
            char c = extension[i];
            quick_key[key_length++] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
        }
        quick_key[key_length] = '\0';
        const char* program = window->controls[program_control].text;
        if (extension[0] && program[0]) sys_reg_set_str(quick_key, program);
    }
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t control_index = 15 + i * 2;
        if (control_index + 1 >= window->control_count) break;
        char key[32];
        copy_text(key, sizeof(key), "USER/startmenu/");
        key[15] = (char)('0' + i);
        key[16] = '\0';
        const char* label = window->controls[control_index].text;
        if (label[0]) sys_reg_set_str(key, label);
    }
}

static void settings_apply(void* context, GuiControl* control);

static void open_settings(void) {
    int id = gui_window_create(&desktop, "Settings", 150, 88, 680, 500, NULL, NULL);
    if (id < 0) return;
    register_window_context(id);
    settings_window = id;
    gui_window_set_close_action(&desktop, id, settings_closed, NULL);
    char background_text[16];
    char accent_text[16];
    uint32_t background = sys_reg_get_u32("USER/desktop/background");
    uint32_t accent = sys_reg_get_u32("USER/desktop/accent");
    if (!background) background = COLOR_BACKGROUND;
    if (!accent) accent = 42948;
    background_text[0] = '0'; background_text[1] = 'x';
    accent_text[0] = '0'; accent_text[1] = 'x';
    static const char hex[] = "0123456789ABCDEF";
    for (uint32_t i = 0; i < 6; i++) {
        background_text[2 + i] = hex[(background >> ((5 - i) * 4)) & 0xF];
        accent_text[2 + i] = hex[(accent >> ((5 - i) * 4)) & 0xF];
    }
    background_text[8] = '\0';
    accent_text[8] = '\0';
    gui_control_add(&desktop, id, GUI_LABEL, 14, 12, 340, 16, "Background color: 0xRRGGBB");
    gui_control_add(&desktop, id, GUI_TEXTFIELD, 14, 36, 210, 26, background_text);
    gui_control_add(&desktop, id, GUI_LABEL, 14, 72, 340, 16, "Accent color: 0xRRGGBB");
    gui_control_add(&desktop, id, GUI_TEXTFIELD, 14, 96, 210, 26, accent_text);
    gui_control_add(&desktop, id, GUI_LABEL, 14, 136, 360, 16, "Settings are stored under USER/desktop.");
    gui_control_add(&desktop, id, GUI_BUTTON, 14, 174, 100, 28, "Apply");
    gui_control_add(&desktop, id, GUI_LABEL, 300, 12, 340, 16, "Serial debug output");
    gui_control_add(&desktop, id, GUI_TEXTFIELD, 300, 36, 90, 26,
                    sys_reg_get_u32("SYSTEM/logging/serial_enabled") ? "1" : "0");
    gui_control_add(&desktop, id, GUI_LABEL, 300, 76, 340, 16, "Quick open extension / program path");
    gui_control_add(&desktop, id, GUI_TEXTFIELD, 300, 100, 90, 26, "ELF");
    char path[128] = {0};
    if (sys_reg_get_str("USER/quickopen/ELF", path, sizeof(path)) < 0 || !path[0]) copy_text(path, sizeof(path), "/bin/SH.ELF");
    gui_control_add(&desktop, id, GUI_TEXTFIELD, 398, 100, 230, 26, path);
    gui_control_add(&desktop, id, GUI_TEXTFIELD, 300, 136, 90, 26, "TXT");
    path[0] = '\0';
    if (sys_reg_get_str("USER/quickopen/TXT", path, sizeof(path)) < 0 || !path[0]) copy_text(path, sizeof(path), "/bin/CAT.ELF");
    gui_control_add(&desktop, id, GUI_TEXTFIELD, 398, 136, 230, 26, path);
    gui_control_add(&desktop, id, GUI_TEXTFIELD, 300, 172, 90, 26, "CFG");
    if (sys_reg_get_str("USER/quickopen/CFG", path, sizeof(path)) < 0 || !path[0]) copy_text(path, sizeof(path), "/bin/CAT.ELF");
    gui_control_add(&desktop, id, GUI_TEXTFIELD, 398, 172, 230, 26, path);
    char start_menu_label[32];
    for (uint32_t i = 0; i < 3; i++) {
        char key[32];
        copy_text(key, sizeof(key), "USER/startmenu/");
        key[15] = (char)('0' + i);
        key[16] = '\0';
        start_menu_label[0] = '\0';
        if (sys_reg_get_str(key, start_menu_label, sizeof(start_menu_label)) < 0 || !start_menu_label[0]) {
            const char* fallback = i == 0 ? "Terminal" : i == 1 ? "Files" : "Notepad";
            copy_text(start_menu_label, sizeof(start_menu_label), fallback);
        }
        gui_control_add(&desktop, id, GUI_LABEL, 300, 210 + (int)i * 32, 120, 16,
                        i == 0 ? "Start menu 0" : i == 1 ? "Start menu 1" : "Start menu 2");
        gui_control_add(&desktop, id, GUI_TEXTFIELD, 430, 206 + (int)i * 32, 180, 26, start_menu_label);
    }
    gui_control_add(&desktop, id, GUI_BUTTON, 300, 300, 100, 28, "Save All");
    desktop.windows[id].action = settings_apply;
    desktop.windows[id].action_context = (void*)(intptr_t)id;
}

static void animate_window_open(int id, int offset_x, int offset_y) {
    if (id < 0 || id >= GUI_MAX_WINDOWS || !desktop.windows[id].used) return;
    GuiWindow* window = &desktop.windows[id];
    int from_x = window->x + offset_x;
    int from_y = window->y + offset_y;
    window->x = from_x;
    window->y = from_y;
    gui_desktop_add_animation(&desktop, id, from_x, from_y, window->normal_x, window->normal_y,
                             sys_ticks(), 220, GUI_ANIM_MOVE);
    window_server_request_redraw(&window_server);
}

static void spawn_desktop_app(const char* path) {
    if (!path || !path[0]) return;
    int pid = sys_fork();
    if (pid == 0) {
        char* argv[] = { (char*)path, NULL };
        char* envp[] = { NULL };
        sys_exec(path, argv, envp);
        sys_exit(127);
    }
}

static void launch_application(int application) {
    if (application == 0) spawn_desktop_app("/bin/SH.ELF");
    else if (application == 1) spawn_desktop_app("/bin/FILES.ELF");
    else if (application == 2) spawn_desktop_app("/bin/NOTES.ELF");
    else if (application == 3) spawn_desktop_app("/bin/REGISTRY.ELF");
    else if (application == 4) spawn_desktop_app("/bin/SETTINGS.ELF");
    else if (application == 5) spawn_desktop_app("/bin/FONTS.ELF");
    else if (application == 6) open_script_studio();
}

static void window_context(void* context, int x, int y) {
    static const char* items[] = { "Minimize", "Maximize / Restore", "Close" };
    gui_context_menu_show(&desktop, x, y, items, 3, (int)(intptr_t)context,
                          window_context_action, context);
}

static void window_context_action(void* context, int item) {
    int id = (int)(intptr_t)context;
    if (id < 0 || id >= GUI_MAX_WINDOWS || !desktop.windows[id].used) return;
    GuiWindow* window = &desktop.windows[id];
    if (item == 0) {
        window->minimized = 1;
        window->visible = 0;
        if (window->focus_action) window->focus_action(window->focus_context, 0);
        if (desktop.focused_window == id) desktop.focused_window = -1;
    } else if (item == 1) {
        if (!window->maximized) {
            window->normal_x = window->x;
            window->normal_y = window->y;
            window->normal_width = window->width;
            window->normal_height = window->height;
            window->x = 0;
            window->y = 0;
            window->width = (int)desktop.width;
            window->height = (int)desktop.height - 28;
            window->maximized = 1;
        } else {
            window->x = window->normal_x;
            window->y = window->normal_y;
            window->width = window->normal_width;
            window->height = window->normal_height;
            window->maximized = 0;
        }
    } else if (item == 2) gui_window_destroy(&desktop, id);
}

static void register_window_context(int id) {
    if (id >= 0) gui_window_set_context_action(&desktop, id, window_context, (void*)(intptr_t)id);
}

static void notify_window_client(int window_id, int input_type, int input_key, int x, int y) {
    if (window_id < 0 || window_id >= GUI_MAX_WINDOWS || !desktop.windows[window_id].used) return;
    GuiWindow* window = &desktop.windows[window_id];
    if (window->owner_pid <= 0) return;
    UserWindowMessage event = {0};
    event.type = USER_WINDOW_MSG_INPUT;
    event.window_id = window_id;
    event.input_type = input_type;
    event.input_key = input_key;
    event.x = x - window->x;
    event.y = y - window->y - 24;
    sys_window_reply(window->owner_pid, &event);
}

static void close_owned_window(int window_id) {
    if (window_id < 0 || window_id >= GUI_MAX_WINDOWS || !desktop.windows[window_id].used) return;
    GuiWindow* window = &desktop.windows[window_id];
    if (window->owner_pid > 0) {
        UserWindowMessage event = {0};
        event.type = USER_WINDOW_MSG_CLOSE;
        event.window_id = window_id;
        sys_window_reply(window->owner_pid, &event);
    }
    gui_window_destroy(&desktop, window_id);
}


int main(int argc, char** argv, char** envp) {
    (void)argc;
    (void)argv;
    (void)envp;

    if (render_init() != 0) return 1;
    uint32_t width = render_width();
    uint32_t height = render_height();

    gui_desktop_init(&desktop, width, height);
    window_server_init(&window_server, &desktop);
    sys_window_register();
    gui_desktop_set_launch_action(&desktop, launch_application);
    gui_desktop_set_key_action(&desktop, files_key_action, NULL);
    uint32_t background = sys_reg_get_u32("USER/desktop/background");
    uint32_t accent = sys_reg_get_u32("USER/desktop/accent");
    if (accent) gui_desktop_set_theme(&desktop, background ? background : COLOR_BACKGROUND, accent);
    uint32_t point_size = sys_reg_get_u32("USER/fonts/size_pt");
    uint32_t font_scale = sys_reg_get_u32("USER/fonts/scale");
    if (point_size >= 6 && point_size <= 36) font_scale = (point_size + 5) / 6;
    if (font_scale < 1) font_scale = 1;
    if (font_scale > 6) font_scale = 6;
    render_set_font_scale(font_scale);
    render_fill_rect(0, 0, (int)width, (int)height, COLOR_BACKGROUND);
    render_draw_text(16, 16, COLOR_MUTED, COLOR_BACKGROUND, "LiveOS desktop | user process");
    gui_desktop_set_launch_action(&desktop, launch_application);

    gui_desktop_draw(&desktop);
    render_present();
    uint32_t last_present_ms = sys_ticks();
    static int drag_initialized = 0;
    static int drag_present_x = 0;
    static int drag_present_y = 0;
    static uint32_t last_drag_present_ms = 0;
    for (;;) {
        uint32_t now_ms = sys_ticks();
        UserWindowMessage incoming;
        while (sys_window_recv(&incoming) == 0) {
            WindowServerMessage request = {0};
            request.type = incoming.type;
            request.window_id = incoming.window_id;
            request.owner_pid = incoming.owner_pid;
            request.x = incoming.x;
            request.y = incoming.y;
            request.width = incoming.width;
            request.height = incoming.height;
            request.control_type = incoming.control_type;
            request.control_id = incoming.control_id;
            request.selected = incoming.selected;
            request.input_type = incoming.input_type;
            request.input_key = incoming.input_key;
            request.result = incoming.result;
            request.payload_size = incoming.payload_size;
            if (incoming.payload_size > 0 && incoming.payload_size < sizeof(request.payload)) {
                for (uint32_t i = 0; i < incoming.payload_size; i++) request.payload[i] = incoming.payload[i];
            }
            WindowServerMessage reply = {0};
            int dispatch_result = window_server_dispatch_message(&window_server, &request, &reply);
            UserWindowMessage response = {0};
            response.type = reply.type;
            response.window_id = reply.window_id;
            response.control_id = reply.control_id;
            response.result = dispatch_result;
            response.owner_pid = 0;
            response.x = reply.x;
            response.y = reply.y;
            response.width = reply.width;
            response.height = reply.height;
            if (incoming.type != USER_WINDOW_MSG_CREATE_CONTROL &&
                incoming.type != USER_WINDOW_MSG_UPDATE_CONTROL) {
                sys_window_reply(request.owner_pid, &response);
            }
        }
        window_server_tick(&window_server, now_ms);
        int old_mouse_x = desktop.mouse_x;
        int old_mouse_y = desktop.mouse_y;
        int dragging_window = -1;
        int drag_width = 0;
        int drag_height = 0;
        for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
            if (desktop.windows[i].used && desktop.windows[i].dragging) {
                dragging_window = i;
                drag_width = desktop.windows[i].width;
                drag_height = desktop.windows[i].height;
                break;
            }
        }
        if (dragging_window >= 0 && !drag_initialized) {
            gui_desktop_draw_excluding(&desktop, dragging_window);
            render_drag_capture();
            drag_present_x = desktop.windows[dragging_window].x;
            drag_present_y = desktop.windows[dragging_window].y;
            last_drag_present_ms = now_ms;
            drag_initialized = 1;
        }
        int changed = gui_desktop_poll(&desktop);
        int drag_active = dragging_window >= 0 && desktop.windows[dragging_window].dragging;
        int drag_ended = dragging_window >= 0 && !desktop.windows[dragging_window].dragging && drag_initialized;
        if (dragging_window >= 0 && now_ms - last_drag_present_ms >= 16) {
            GuiWindow* drag_window = &desktop.windows[dragging_window];
            render_drag_restore_union(drag_present_x, drag_present_y,
                                      drag_window->x, drag_window->y,
                                      drag_width, drag_height);
            gui_desktop_draw_window(&desktop, dragging_window);
            render_present_union(drag_present_x, drag_present_y,
                                 drag_window->x, drag_window->y,
                                 drag_width, drag_height);
            drag_present_x = drag_window->x;
            drag_present_y = drag_window->y;
            last_drag_present_ms = now_ms;
        }
        if (dragging_window < 0 || !desktop.windows[dragging_window].dragging) {
            drag_initialized = 0;
        }
        if (desktop.mouse_x != old_mouse_x || desktop.mouse_y != old_mouse_y) {
            render_present_cursor(old_mouse_x, old_mouse_y, desktop.mouse_x, desktop.mouse_y);
        }
        if (!drag_active && (changed > 0 || window_server.pending_redraw) &&
            now_ms - last_present_ms >= 33) {
            gui_desktop_draw(&desktop);
            render_present();
            window_server.pending_redraw = 0;
            last_present_ms = now_ms;
        }
        if (drag_ended) {
            gui_desktop_draw(&desktop);
            render_present();
            window_server.pending_redraw = 0;
            last_present_ms = now_ms;
        }
        if (dragging_window < 0 || drag_ended) {
            drag_initialized = 0;
        }
    }
}
