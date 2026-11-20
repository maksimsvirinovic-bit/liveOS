#include "framebuffer.h"
#include "paging.h"
#include "vmalloc.h"
#include <stdint.h>
#include <serial/serial.h>
#include <vmm.h>

static Framebuffer g_fb;
extern const uint8_t font8x8_basic[128][8];

int fb_init(const BOOT_INFO* bi) {
    if (!bi || !bi->FbAddr) return -1;

    uint64_t fb_size = (uint64_t)bi->Pitch * bi->Height;
    uint64_t fb_offset = bi->FbAddr & (PAGE_SIZE - 1);
    uint64_t fb_phys_base = bi->FbAddr & ~(PAGE_SIZE - 1);
    uint64_t map_size = (fb_offset + fb_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t fb_pages = map_size / PAGE_SIZE;
    uint64_t map_virt = vmalloc_reserve_mmio(map_size);
    if (!map_virt) return -1;

    for (uint64_t i = 0; i < fb_pages; i++) {
        if (paging_map_4k(vmm_kernel()->pml4_phys,
                          fb_phys_base + i * PAGE_SIZE,
                          map_virt + i * PAGE_SIZE,
                          PAGE_PRESENT | PAGE_WRITE | PAGE_NOCACHE) != 0) {
            serial_emergency_print("fb: map failed\n");
            return -1;
        }
        if (paging_get_phys(vmm_kernel()->pml4_phys, map_virt + i * PAGE_SIZE) !=
            ((fb_phys_base + i * PAGE_SIZE) & PAGE_ADDR_MASK)) {
            serial_emergency_print("fb: mapping verification failed at page ");
            serial_emergency_print_hex(i);
            serial_emergency_print("\n");
            return -1;
        }
    }

    g_fb.pixels      = (uint32_t*)(uintptr_t)(map_virt + fb_offset);
    g_fb.phys_base   = fb_phys_base;
    g_fb.map_size    = map_size;
    g_fb.offset      = (uint32_t)fb_offset;
    g_fb.width       = bi->Width;
    g_fb.height      = bi->Height;
    g_fb.pitch_pixels = bi->Pitch / 4;
    g_fb.bpp         = bi->Bpp;

    serial_emergency_print("fb: ");
    serial_emergency_print_hex(bi->Width);
    serial_emergency_print("x");
    serial_emergency_print_hex(bi->Height);
    serial_emergency_print(" bpp=");
    serial_emergency_print_hex(bi->Bpp);
    serial_emergency_print(" @");
    serial_emergency_print_hex((uint64_t)(uintptr_t)g_fb.pixels);
    serial_emergency_print(" size=");
    serial_emergency_print_hex(fb_size);
    serial_emergency_print("\n");

    return 0;
}

Framebuffer* fb_get(void) { return &g_fb; }

void fb_clear(uint32_t color) {
    if (!g_fb.pixels) return;
    uint64_t total = (uint64_t)g_fb.pitch_pixels * g_fb.height;
    uint32_t* p = g_fb.pixels;
    for (uint32_t i = 0; i < total; i++) p[i] = color;
}

void fb_set_pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || (uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height) return;
    uint32_t* p = g_fb.pixels;
    p[y * g_fb.pitch_pixels + x] = color;
}

void fb_fill_rect(int x, int y, uint32_t width, uint32_t height, uint32_t color) {
    if (!g_fb.pixels || width == 0 || height == 0 || x < 0 || y < 0) return;
    if ((uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height) return;

    uint32_t max_x = g_fb.width - (uint32_t)x;
    uint32_t max_y = g_fb.height - (uint32_t)y;
    if (width > max_x) width = max_x;
    if (height > max_y) height = max_y;
    if (width == 0 || height == 0) return;

    for (uint32_t row = 0; row < height; row++) {
        uint32_t* dst = g_fb.pixels + (uint64_t)((uint32_t)y + row) * g_fb.pitch_pixels + (uint32_t)x;
        for (uint32_t col = 0; col < width; col++) dst[col] = color;
    }
}

void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    uint8_t ch = (uint8_t)c;
    if (ch >= 128) ch = '?';

    for (int row = 0; row < 8; row++) {
        int py = y + row;
        if (py < 0 || (uint32_t)py >= g_fb.height) continue;
        uint8_t bits = font8x8_basic[ch][row];
        for (int col = 0; col < 8; col++) {
            int px = x + col;
            if (px < 0 || (uint32_t)px >= g_fb.width) continue;
            uint32_t color = (bits & (1 << col)) ? fg : bg;
            fb_set_pixel(px, py, color);
        }
    }
}

void fb_draw_string(int x, int y, const char* s, uint32_t fg, uint32_t bg) {
    if (!s) return;
    while (*s && x < (int)g_fb.width) {
        fb_draw_char(x, y, *s, fg, bg);
        x += 8;
        s++;
    }
}