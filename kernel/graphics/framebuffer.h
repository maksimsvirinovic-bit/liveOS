#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <boot_info.h>

typedef struct {
    uint32_t* pixels;
    uint64_t  phys_base;
    uint64_t  map_size;
    uint32_t  offset;
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch_pixels;
    uint32_t  bpp;
} Framebuffer;

int  fb_init(const BOOT_INFO* bi);
Framebuffer* fb_get(void);

void fb_clear(uint32_t color);
void fb_set_pixel(int x, int y, uint32_t color);
void fb_fill_rect(int x, int y, uint32_t width, uint32_t height, uint32_t color);
void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void fb_draw_string(int x, int y, const char* s, uint32_t fg, uint32_t bg);

#define RGB(r, g, b)  (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define COLOR_BLACK   0x00000000
#define COLOR_WHITE   0x00FFFFFF
#define COLOR_GRAY    0x00808080

#endif