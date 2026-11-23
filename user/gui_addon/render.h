#ifndef LIVEOS_DESKTOP_RENDER_H
#define LIVEOS_DESKTOP_RENDER_H

#include <stdint.h>

int render_init(void);
void render_set_font_scale(uint32_t scale);
uint32_t render_width(void);
uint32_t render_height(void);
void render_fill_rect(int x, int y, int width, int height, uint32_t color);
void render_draw_text(int x, int y, uint32_t fg, uint32_t bg, const char* text);
void render_present(void);
void render_present_cursor(int old_x, int old_y, int x, int y);
void render_present_rect(int x, int y, int width, int height);
void render_present_union(int x1, int y1, int x2, int y2, int width, int height);
void render_drag_capture(void);
void render_drag_restore(void);
void render_drag_restore_union(int x1, int y1, int x2, int y2, int width, int height);

#endif