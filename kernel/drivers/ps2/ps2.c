#include "ps2.h"
#include "tasking/scheduler.h"
#include "x86_64/io.h"
#include "x86_64/interrupts/irq.h"
#include <serial/serial.h>
#include <str.h>
#include <stddef.h>

#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_CMD     0x64

#define PS2_STATUS_OUTPUT_FULL  0x01
#define PS2_STATUS_INPUT_FULL   0x02

#define PS2_CMD_READ_CONFIG     0x20
#define PS2_CMD_WRITE_CONFIG    0x60
#define PS2_CMD_DISABLE_KBD     0xAD
#define PS2_CMD_ENABLE_KBD      0xAE
#define PS2_CMD_DISABLE_MOUSE   0xA7
#define PS2_CMD_ENABLE_MOUSE    0xA8
#define PS2_CMD_SEND_TO_MOUSE   0xD4
#define PS2_CMD_TEST_CTRL       0xAA
#define MOUSE_CMD_SET_SAMPLE    0xF3
#define MOUSE_CMD_ENABLE_DATA   0xF4
#define MOUSE_CMD_RESET         0xFF
#define MOUSE_ACK               0xFA

static int g_irq_kbd   = -1;
static int g_irq_mouse = -1;

#define KBD_BUF_SIZE  128
static volatile int  g_kbd_head = 0;
static volatile int  g_kbd_tail = 0;
static volatile int  g_kbd_buf[KBD_BUF_SIZE];

static volatile int g_shift = 0;
static volatile int g_ctrl  = 0;
static volatile int g_alt   = 0;
static volatile int g_caps  = 0;
static volatile int g_tty_focus = 0;
static volatile uint8_t g_tty_input[512];
static volatile uint32_t g_tty_head;
static volatile uint32_t g_tty_tail;
static volatile uint8_t g_tty_edit[256];
static volatile uint32_t g_tty_edit_length;
static volatile uint8_t g_tty_output[8192];
static volatile uint32_t g_tty_out_head;
static volatile uint32_t g_tty_out_tail;
static volatile int g_pointer_x = 0;
static volatile int g_pointer_y = 0;
static volatile int g_pointer_position_initialized = 0;
static volatile uint32_t g_pointer_buttons = 0;
static volatile uint8_t g_mouse_cycle = 0;
static volatile uint8_t g_mouse_packet[3];
static volatile int g_pointer_width = 1024;
static volatile int g_pointer_height = 768;
static volatile uint32_t g_mouse_debug_packets = 0;

void ps2_set_pointer_bounds(uint32_t width, uint32_t height) {
    if (width) g_pointer_width = (int)width;
    if (height) g_pointer_height = (int)height;
    if (!g_pointer_position_initialized && width && height) {
        g_pointer_x = (int)(width / 2);
        g_pointer_y = (int)(height / 2);
        g_pointer_position_initialized = 1;
    }
    if (g_pointer_x >= g_pointer_width) g_pointer_x = g_pointer_width - 1;
    if (g_pointer_y >= g_pointer_height) g_pointer_y = g_pointer_height - 1;
}

void ps2_get_pointer_state(PointerState* state) {
    if (!state) return;
    state->x = g_pointer_x;
    state->y = g_pointer_y;
    state->buttons = g_pointer_buttons;
}

static int ps2_wait_input_clear(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb(PS2_STATUS) & PS2_STATUS_INPUT_FULL)) return 0;
    }
    return -1;
}

static int ps2_wait_output_full(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS) & PS2_STATUS_OUTPUT_FULL) return 0;
    }
    return -1;
}

static int ps2_write_cmd(uint8_t cmd) {
    if (ps2_wait_input_clear() != 0) return -1;
    outb(PS2_CMD, cmd);
    return 0;
}

static int ps2_write_data(uint8_t data) {
    if (ps2_wait_input_clear() != 0) return -1;
    outb(PS2_DATA, data);
    return 0;
}

static int ps2_read_data(uint8_t* out) {
    if (ps2_wait_output_full() != 0) return -1;
    *out = inb(PS2_DATA);
    return 0;
}

static int ps2_flush_output(void) {
    for (int i = 0; i < 100; i++) {
        if (!(inb(PS2_STATUS) & PS2_STATUS_OUTPUT_FULL)) break;
        inb(PS2_DATA);
    }
    return 0;
}

static int mouse_write_cmd(uint8_t command) {
    if (ps2_write_cmd(PS2_CMD_SEND_TO_MOUSE) != 0 || ps2_write_data(command) != 0) return -1;
    uint8_t response;
    for (int retry = 0; retry < 10; retry++) {
        if (ps2_read_data(&response) == 0 && response == MOUSE_ACK) return 0;
    }
    return -1;
}

static void kbd_push(int c) {
    int next = (g_kbd_head + 1) % KBD_BUF_SIZE;
    if (next == g_kbd_tail) return;
    g_kbd_buf[g_kbd_head] = c;
    g_kbd_head = next;
}

static void tty_push(int c) {
    uint32_t next = (g_tty_head + 1) % sizeof(g_tty_input);
    if (next == g_tty_tail) return;
    g_tty_input[g_tty_head] = (uint8_t)c;
    g_tty_head = next;
}

int kbd_getchar(void) {
    if (g_kbd_tail == g_kbd_head) return 0;
    int c = g_kbd_buf[g_kbd_tail];
    g_kbd_tail = (g_kbd_tail + 1) % KBD_BUF_SIZE;
    return c;
}

int kbd_haschar(void) {
    return g_kbd_head != g_kbd_tail;
}

void kbd_set_tty_focus(int enabled) { g_tty_focus = enabled != 0; }
int kbd_tty_focus(void) { return g_tty_focus; }

void tty_clear_input(void) {
    g_tty_edit_length = 0;
    for (uint32_t i = 0; i < sizeof(g_tty_edit); i++) g_tty_edit[i] = 0;
}

void tty_reset(void) {
    g_tty_focus = 0;
    tty_clear_input();
    g_tty_head = g_tty_tail = 0;
    g_tty_out_head = g_tty_out_tail = 0;
    for (uint32_t i = 0; i < sizeof(g_tty_output); i++) g_tty_output[i] = 0;
}

int tty_input_read(char* buffer, uint32_t capacity) {
    if (!buffer || capacity == 0) return -1;
    uint32_t count = 0;
    while (count + 1 < capacity && g_tty_tail != g_tty_head) {
        uint8_t c = g_tty_input[g_tty_tail];
        g_tty_tail = (g_tty_tail + 1) % sizeof(g_tty_input);
        buffer[count++] = (char)c;
        if (c == '\n') break;
    }
    if (count) buffer[count] = '\0';
    return (int)count;
}

static void tty_echo_char(uint8_t c) {
    uint32_t next = (g_tty_out_head + 1) % sizeof(g_tty_output);
    if (next == g_tty_out_tail) g_tty_out_tail = (g_tty_out_tail + 1) % sizeof(g_tty_output);
    g_tty_output[g_tty_out_head] = c;
    g_tty_out_head = next;
}

static void tty_receive_key(int key) {
    if (key == '\b') {
        if (g_tty_edit_length) {
            g_tty_edit_length--;
            tty_echo_char('\b');
        }
        return;
    }
    if (key == '\n') {
        for (uint32_t i = 0; i < g_tty_edit_length; i++) tty_push(g_tty_edit[i]);
        tty_push('\n');
        g_tty_edit_length = 0;
        tty_echo_char('\r');
        tty_echo_char('\n');
        return;
    }
    if (key >= 32 && key < 127 && g_tty_edit_length + 1 < sizeof(g_tty_edit)) {
        g_tty_edit[g_tty_edit_length++] = (uint8_t)key;
        tty_echo_char((uint8_t)key);
    }
}

void tty_output_write(const char* buffer, uint32_t length) {
    if (!buffer) return;
    for (uint32_t i = 0; i < length; i++) {
        uint32_t next = (g_tty_out_head + 1) % sizeof(g_tty_output);
        if (next == g_tty_out_tail) g_tty_out_tail = (g_tty_out_tail + 1) % sizeof(g_tty_output);
        g_tty_output[g_tty_out_head] = (uint8_t)buffer[i];
        g_tty_out_head = next;
    }
}

uint32_t tty_output_read(char* buffer, uint32_t capacity) {
    if (!buffer) return 0;
    uint32_t count = 0;
    while (count < capacity && g_tty_out_tail != g_tty_out_head) {
        buffer[count++] = (char)g_tty_output[g_tty_out_tail];
        g_tty_out_tail = (g_tty_out_tail + 1) % sizeof(g_tty_output);
    }
    return count;
}

static const char kbd_map_lower[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0
};

static const char kbd_map_upper[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0
};

static int kbd_special(uint8_t sc) {
    switch (sc) {
        case 0x48: return KBD_KEY_UP;
        case 0x50: return KBD_KEY_DOWN;
        case 0x4B: return KBD_KEY_LEFT;
        case 0x4D: return KBD_KEY_RIGHT;
        case 0x47: return KBD_KEY_HOME;
        case 0x4F: return KBD_KEY_END;
        case 0x49: return KBD_KEY_PGUP;
        case 0x51: return KBD_KEY_PGDN;
        case 0x53: return KBD_KEY_DELETE;
        case 0x52: return KBD_KEY_INSERT;
        case 0x01: return KBD_KEY_ESC;
        case 0x0E: return KBD_KEY_BACKSPACE;
        case 0x0F: return KBD_KEY_TAB;
        case 0x1C: return KBD_KEY_ENTER;
        case 0x1D: return KBD_KEY_LCTRL;
        case 0x2A: return KBD_KEY_LSHIFT;
        case 0x36: return KBD_KEY_LSHIFT;
        case 0x38: return KBD_KEY_LALT;
        case 0x3A: return KBD_KEY_CAPS;
        default:   return 0;
    }
}


static void kbd_irq_handler(uint32_t irq, void* ctx) {
    (void)irq; (void)ctx;

    uint8_t sc = inb(PS2_DATA);

    if (sc == 0xE0) {
        return;
    }

    int released = (sc & 0x80) != 0;
    uint8_t base = sc & 0x7F;

    if (released) {
        if (base == 0x2A || base == 0x36) g_shift = 0;
        if (base == 0x1D) g_ctrl = 0;
        if (base == 0x38) g_alt = 0;
        return;
    }

    if (base == 0x2A || base == 0x36) { g_shift = 1; return; }
    if (base == 0x1D) { g_ctrl = 1; return; }
    if (base == 0x38) { g_alt = 1; return; }
    if (base == 0x3A) { g_caps ^= 1; return; }

    int sp = kbd_special(base);
    if (sp == KBD_KEY_ENTER) { if (g_tty_focus) tty_receive_key('\n'); else kbd_push('\n'); return; }
    if (sp == KBD_KEY_BACKSPACE) { if (g_tty_focus) tty_receive_key('\b'); else kbd_push('\b'); return; }
    if (sp < 0) { kbd_push(sp); return; }

    int upper = g_shift ^ g_caps;
    char ch = upper ? kbd_map_upper[base] : kbd_map_lower[base];
    if (ch != 0) {
        if (g_tty_focus) {
            tty_receive_key((unsigned char)ch);
        } else {
            kbd_push((int)(unsigned char)ch);
        }
    }
}

static void mouse_irq_handler(uint32_t irq, void* ctx) {
    (void)irq;
    (void)ctx;
    if (!(inb(PS2_STATUS) & PS2_STATUS_OUTPUT_FULL)) return;
    uint8_t data = inb(PS2_DATA);
    if (g_mouse_cycle == 0 && !(data & 0x08)) return;
    g_mouse_packet[g_mouse_cycle++] = data;
    if (g_mouse_cycle < 3) return;
    g_mouse_cycle = 0;

    uint8_t flags = g_mouse_packet[0];
    if (flags & 0xC0) return;
    int x = g_pointer_x + (int8_t)g_mouse_packet[1];
    int y = g_pointer_y - (int8_t)g_mouse_packet[2];
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= g_pointer_width) x = g_pointer_width - 1;
    if (y >= g_pointer_height) y = g_pointer_height - 1;
    g_pointer_x = x;
    g_pointer_y = y;
    g_pointer_buttons = flags & 0x07;
    if (g_mouse_debug_packets < 3) {
        serial_emergency_print("ps2: mouse packet x=");
        serial_emergency_print_hex((uint64_t)(uint32_t)x);
        serial_emergency_print(" y=");
        serial_emergency_print_hex((uint64_t)(uint32_t)y);
        serial_emergency_print(" buttons=");
        serial_emergency_print_hex(g_pointer_buttons);
        serial_emergency_print("\n");
        g_mouse_debug_packets++;
    }
}


void ps2_init(void) {
    serial_emergency_print("ps2: init\n");

    ps2_write_cmd(PS2_CMD_DISABLE_KBD);
    ps2_write_cmd(PS2_CMD_DISABLE_MOUSE);
    ps2_flush_output();

    ps2_write_cmd(PS2_CMD_TEST_CTRL);
    uint8_t t;
    if (ps2_read_data(&t) != 0 || t != 0x55) {
        serial_emergency_print("ps2: controller self-test failed\n");
    }

    ps2_write_cmd(PS2_CMD_READ_CONFIG);
    uint8_t cfg;
    if (ps2_read_data(&cfg) != 0) {
        serial_emergency_print("ps2: cannot read config\n");
        return;
    }

    cfg |= 0x01;
    cfg |= 0x02;
    cfg &= ~0x10;
    cfg &= ~0x20;
    cfg |= 0x40;
    ps2_write_cmd(PS2_CMD_WRITE_CONFIG);
    ps2_write_data(cfg);

    ps2_write_cmd(PS2_CMD_ENABLE_KBD);
    ps2_write_cmd(PS2_CMD_ENABLE_MOUSE);

    mouse_write_cmd(MOUSE_CMD_RESET);
    uint8_t response;
    ps2_read_data(&response);
    ps2_read_data(&response);
    mouse_write_cmd(MOUSE_CMD_SET_SAMPLE);
    ps2_write_cmd(PS2_CMD_SEND_TO_MOUSE);
    ps2_write_data(100);
    ps2_read_data(&response);
    mouse_write_cmd(MOUSE_CMD_ENABLE_DATA);

    g_irq_kbd   = 1;
    g_irq_mouse = 12;

    irq_register(g_irq_kbd,   kbd_irq_handler,   NULL);
    irq_register(g_irq_mouse, mouse_irq_handler, NULL);

    serial_emergency_print("ps2: keyboard and pointer ready\n");
}

int input_getchar_blocking(void) {
    __asm__ volatile("sti" ::: "memory");
    for (;;) {
        int c = kbd_getchar();
        if (c != 0) {
            if (c == '\b') {
                serial_emergency_putc('\b');
                serial_emergency_putc(' ');
                serial_emergency_putc('\b');
            } else if (c == '\n') {
                serial_emergency_putc('\n');
            } else if (c >= 32 && c < 127) {
                serial_emergency_putc((char)c);
            }
            return c;
        }
        scheduler_yield();
    }
}