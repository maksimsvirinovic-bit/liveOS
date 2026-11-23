#include "render.h"
#include "../sys.h"

extern const char font8x8_basic[128][8];

static volatile uint32_t* framebuffer_pixels;
static uint32_t* backbuffer;
static uint32_t screen_width;
static uint32_t screen_height;
static uint32_t pitch_pixels;
static uint32_t font_scale = 1;
static uint32_t* drag_snapshot;

int render_init(void) {
    UserFramebuffer framebuffer = {0};
    if (sys_fb_map(&framebuffer) != 0 || !framebuffer.pixels ||
        !framebuffer.width || !framebuffer.height ||
        framebuffer.pitch_pixels < framebuffer.width || framebuffer.bpp != 32) {
        return -1;
    }
    uint64_t backbuffer_size = (uint64_t)framebuffer.width * framebuffer.height * sizeof(uint32_t);
    if (backbuffer_size / sizeof(uint32_t) / framebuffer.width != framebuffer.height) return -1;
    backbuffer = (uint32_t*)sys_mmap_anon(backbuffer_size);
    if (!backbuffer) return -1;
    framebuffer_pixels = (volatile uint32_t*)(uintptr_t)framebuffer.pixels;
    screen_width = framebuffer.width;
    screen_height = framebuffer.height;
    pitch_pixels = framebuffer.pitch_pixels;
    return 0;
}

uint32_t render_width(void) { return screen_width; }
uint32_t render_height(void) { return screen_height; }

void render_set_font_scale(uint32_t scale) {
    if (scale >= 1 && scale <= 6) font_scale = scale;
}

void render_fill_rect(int x, int y, int width, int height, uint32_t color) {
    if (!backbuffer || width <= 0 || height <= 0) return;
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x >= (int)screen_width || y >= (int)screen_height) return;
    if (width > (int)screen_width - x) width = (int)screen_width - x;
    if (height > (int)screen_height - y) height = (int)screen_height - y;
    if (width <= 0 || height <= 0) return;

    for (int row = 0; row < height; row++) {
        uint32_t* dst = backbuffer + (uint64_t)(y + row) * screen_width + x;
        for (int col = 0; col < width; col++) dst[col] = color;
    }
}

static void draw_char(int x, int y, uint8_t character, uint32_t fg, uint32_t bg) {
    if (character >= 128) character = '?';
    for (int row = 0; row < 8; row++) {
        uint8_t bits = (uint8_t)font8x8_basic[character][row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = (bits & (1u << col)) ? fg : bg;
            for (uint32_t sy = 0; sy < font_scale; sy++) {
                int py = y + row * (int)font_scale + (int)sy;
                if (py < 0 || py >= (int)screen_height) continue;
                for (uint32_t sx = 0; sx < font_scale; sx++) {
                    int px = x + col * (int)font_scale + (int)sx;
                    if (px >= 0 && px < (int)screen_width) {
                        backbuffer[(uint64_t)py * screen_width + px] = color;
                    }
                }
            }
        }
    }
}

void render_draw_text(int x, int y, uint32_t fg, uint32_t bg, const char* text) {
    if (!backbuffer || !text || y >= (int)screen_height || y + 8 * (int)font_scale <= 0) return;
    while (*text && x < (int)screen_width) {
        draw_char(x, y, (uint8_t)*text++, fg, bg);
        x += 8 * (int)font_scale;
    }
}

void render_present(void) {
    if (!framebuffer_pixels || !backbuffer) return;
    for (uint32_t y = 0; y < screen_height; y++) {
        const uint32_t* src = backbuffer + (uint64_t)y * screen_width;
        volatile uint32_t* dst = framebuffer_pixels + (uint64_t)y * pitch_pixels;
        for (uint32_t x = 0; x < screen_width; x++) dst[x] = src[x];
    }
}

void render_present_cursor(int old_x, int old_y, int x, int y) {
    if (!framebuffer_pixels || !backbuffer) return;

    static const uint8_t cursor_widths[18] = {
        2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 11, 8, 5, 3
    };

    int old_left = old_x + 1;
    int old_top = old_y;
    int old_right = old_x + 16;
    int old_bottom = old_y + 18;

    int left = x + 1;
    int top = y;
    int right = x + 16;
    int bottom = y + 18;

    if (old_left < 0) old_left = 0;
    if (old_top < 0) old_top = 0;
    if (old_right > (int)screen_width) old_right = (int)screen_width;
    if (old_bottom > (int)screen_height) old_bottom = (int)screen_height;

    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > (int)screen_width) right = (int)screen_width;
    if (bottom > (int)screen_height) bottom = (int)screen_height;

    for (int py = old_top; py < old_bottom; py++) {
        for (int px = old_left; px < old_right; px++) {
            framebuffer_pixels[(uint64_t)py * pitch_pixels + px] =
                backbuffer[(uint64_t)py * screen_width + px];
        }
    }

    for (int py = top; py < bottom; py++) {
        for (int px = left; px < right; px++) {
            framebuffer_pixels[(uint64_t)py * pitch_pixels + px] =
                backbuffer[(uint64_t)py * screen_width + px];
        }
    }

    for (int row = 0; row < 18; row++) {
        int width = cursor_widths[row];
        for (int col = 0; col < width; col++) {
            int px = x + 1 + col;
            int py = y + row;
            if (px < 0 || py < 0 || px >= (int)screen_width || py >= (int)screen_height) continue;
            uint32_t color = col < width - 3 ? 0x00FFFFFFu : 0x00000000u;
            framebuffer_pixels[(uint64_t)py * pitch_pixels + px] = color;
        }
    }
}

void render_present_rect(int x, int y, int width, int height) {
    if (!framebuffer_pixels || !backbuffer || width <= 0 || height <= 0) return;
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x >= (int)screen_width || y >= (int)screen_height) return;
    if (width > (int)screen_width - x) width = (int)screen_width - x;
    if (height > (int)screen_height - y) height = (int)screen_height - y;
    for (int py = y; py < y + height; py++) {
        const uint32_t* src = backbuffer + (uint64_t)py * screen_width + x;
        volatile uint32_t* dst = framebuffer_pixels + (uint64_t)py * pitch_pixels + x;
        for (int px = 0; px < width; px++) dst[px] = src[px];
    }
}

void render_present_union(int x1, int y1, int x2, int y2, int width, int height) {
    int left = x1 < x2 ? x1 : x2;
    int top = y1 < y2 ? y1 : y2;
    int right1 = x1 + width + 8;
    int right2 = x2 + width + 8;
    int bottom1 = y1 + height + 8;
    int bottom2 = y2 + height + 8;
    int right = right1 > right2 ? right1 : right2;
    int bottom = bottom1 > bottom2 ? bottom1 : bottom2;
    render_present_rect(left, top, right - left, bottom - top);
}

void render_drag_capture(void) {
    if (!backbuffer) return;
    uint64_t size = (uint64_t)screen_width * screen_height;
    if (!drag_snapshot) drag_snapshot = (uint32_t*)sys_mmap_anon(size * sizeof(uint32_t));
    if (!drag_snapshot) return;
    for (uint64_t i = 0; i < size; i++) drag_snapshot[i] = backbuffer[i];
}

void render_drag_restore_union(int x1, int y1, int x2, int y2, int width, int height) {
    if (!backbuffer || !drag_snapshot) return;
    int left = x1 < x2 ? x1 : x2;
    int top = y1 < y2 ? y1 : y2;
    int right1 = x1 + width + 8;
    int right2 = x2 + width + 8;
    int bottom1 = y1 + height + 8;
    int bottom2 = y2 + height + 8;
    int right = right1 > right2 ? right1 : right2;
    int bottom = bottom1 > bottom2 ? bottom1 : bottom2;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > (int)screen_width) right = (int)screen_width;
    if (bottom > (int)screen_height) bottom = (int)screen_height;
    for (int py = top; py < bottom; py++) {
        for (int px = left; px < right; px++) {
            backbuffer[(uint64_t)py * screen_width + px] = drag_snapshot[(uint64_t)py * screen_width + px];
        }
    }
}

void render_drag_restore(void) {
    if (!backbuffer || !drag_snapshot) return;
    uint64_t size = (uint64_t)screen_width * screen_height;
    for (uint64_t i = 0; i < size; i++) backbuffer[i] = drag_snapshot[i];
}