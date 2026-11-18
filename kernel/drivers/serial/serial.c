#include "serial.h"
#include <x86_64/io.h>
#include <stddef.h>

#define COM1_PORT 0x3F8

static volatile int g_serial_debug_enabled = 0;

void serial_set_debug_enabled(int enabled) { g_serial_debug_enabled = enabled != 0; }
int serial_debug_enabled(void) { return g_serial_debug_enabled; }

void serial_emergency_init(void) {
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
}

void serial_force_putc(char c) {
    while ((inb(COM1_PORT + 5) & 0x20) == 0) {}
    outb(COM1_PORT, (uint8_t)c);
}

void serial_force_print(const char* text) {
    if (!text) return;
    while (*text) {
        if (*text == '\n') serial_force_putc('\r');
        serial_force_putc(*text++);
    }
}

void serial_emergency_putc(char c) {
    if (g_serial_debug_enabled) serial_force_putc(c);
}

void serial_debug_putc(char c) {
    if (g_serial_debug_enabled) serial_force_putc(c);
}

void serial_debug_print(const char* text) {
    if (!g_serial_debug_enabled || !text) return;
    while (*text) {
        if (*text == '\n') serial_debug_putc('\r');
        serial_debug_putc(*text++);
    }
}

void serial_emergency_print(const char* s) {
    while (*s) {
        if (*s == '\n') serial_emergency_putc('\r');
        serial_emergency_putc(*s++);
    }
}

void serial_emergency_print_hex(uint64_t v) {
    serial_emergency_print("0x");
    if (v == 0) {
        serial_emergency_putc('0');
        return;
    }
    const char hex[] = "0123456789ABCDEF";

    int shift = 60;
    while (shift > 0 && ((v >> shift) & 0xF) == 0) shift -= 4;
    for (int i = shift; i >= 0; i -= 4) {
        serial_emergency_putc(hex[(v >> i) & 0xF]);
    }
}

void serial_emergency_dec(uint64_t value) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (value == 0) { serial_emergency_putc('0'); return; }
    while (value) { buf[--i] = '0' + (value % 10); value /= 10; }
    serial_emergency_print(&buf[i]);
}

static void serial_putc_op(char c) {
    if (c == '\n') serial_debug_putc('\r');
    serial_debug_putc(c);
}

void serial_print_op(const char* s) {
    serial_debug_print(s);
}

static int serial_getchar_op(void) { return 0; }
static int serial_kbhit_op(void)   { return 0; }
static void serial_set_color_op(uint32_t fg, uint32_t bg) { (void)fg; (void)bg; }
static void serial_reset_color_op(void) {}

static CharDriverOps serial_ops = {
    .putc = serial_putc_op,
    .getchar = serial_getchar_op,
    .kbhit = serial_kbhit_op,
    .set_color = serial_set_color_op,
    .reset_color = serial_reset_color_op,
};

static int serial_driver_init(Driver* self) {
    (void)self;
    serial_emergency_init();
    return 0;
}

static Driver serial_driver = {
    .name = "serial",
    .type = DRIVER_TYPE_CHAR,
    .init_level = DRIVER_INIT_EARLY,
    .init = serial_driver_init,
    .deinit = NULL,
    .private_data = &serial_ops,
    .next = NULL,
};

void serial_driver_register(void) {
    driver_register(&serial_driver);
}