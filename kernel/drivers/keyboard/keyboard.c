#include "keyboard.h"
#include <stddef.h>

static int keyboard_getchar_op(void) { return 0; }
static int keyboard_kbhit_op(void)   { return 0; }
static void keyboard_putc_op(char c) { (void)c; }
static void keyboard_set_color_op(uint32_t fg, uint32_t bg) { (void)fg; (void)bg; }
static void keyboard_reset_color_op(void) {}

static CharDriverOps keyboard_ops = {
    .putc = keyboard_putc_op,
    .getchar = keyboard_getchar_op,
    .kbhit = keyboard_kbhit_op,
    .set_color = keyboard_set_color_op,
    .reset_color = keyboard_reset_color_op,
};

static int keyboard_driver_init(Driver* self) {
    (void)self;
    return 0;
}

static Driver keyboard_driver = {
    .name = "keyboard",
    .type = DRIVER_TYPE_CHAR,
    .init_level = DRIVER_INIT_EARLY,
    .init = keyboard_driver_init,
    .deinit = NULL,
    .private_data = &keyboard_ops,
    .next = NULL,
};

void keyboard_driver_register(void) {
    driver_register(&keyboard_driver);
}