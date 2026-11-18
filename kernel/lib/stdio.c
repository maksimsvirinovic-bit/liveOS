#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <ops/driver.h>
#include <serial/serial.h>
#include <stddef.h>


static CharDriverOps* g_console = NULL;

static void stdio_pick_primary(void) {
    if (!g_console) g_console = char_driver_get_primary();
}

void stdio_init(void) {
    stdio_pick_primary();
}


void putchar(char c) {
    stdio_pick_primary();
    if (g_console && g_console->putc) {
        g_console->putc(c);
    } else {
        serial_debug_putc(c);
    }
}

void print(const char* s) {
    while (*s) putchar(*s++);
}

void puts(const char* s) {
    print(s);
    putchar('\n');
}

void print_hex(uint64_t v) {
    char buf[20];
    snprintf(buf, sizeof(buf), "0x%lx", v);
    print(buf);
}

void print_dec(uint64_t v) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%lu", v);
    print(buf);
}

void stdio_set_color(uint32_t fg, uint32_t bg) {
    stdio_pick_primary();
    if (g_console && g_console->set_color) g_console->set_color(fg, bg);
}

void stdio_reset_color(void) {
    stdio_pick_primary();
    if (g_console && g_console->reset_color) g_console->reset_color();
}


int vprintf(const char* fmt, va_list args) {
    int written = 0;
    while (*fmt) {
        if (*fmt != '%') { putchar(*fmt++); written++; continue; }
        fmt++;

        int zero_pad = 0;
        if (*fmt == '0') { zero_pad = 1; fmt++; }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        (void)zero_pad; (void)width;

        int long_arg = 0;
        if (*fmt == 'l') { long_arg = 1; fmt++; if (*fmt == 'l') { long_arg = 2; fmt++; } }

        switch (*fmt) {
            case 'd': case 'i': {
                int64_t v = long_arg ? va_arg(args, int64_t) : va_arg(args, int);
                if (v < 0) { putchar('-'); v = -v; written++; }
                print_dec((uint64_t)v);
                break;
            }
            case 'u': {
                uint64_t v = long_arg ? va_arg(args, uint64_t) : va_arg(args, uint32_t);
                print_dec(v);
                break;
            }
            case 'x': case 'X': {
                uint64_t v = long_arg ? va_arg(args, uint64_t) : va_arg(args, uint32_t);
                print_hex(v);
                break;
            }
            case 'p': {
                void* v = va_arg(args, void*);
                print_hex((uint64_t)v);
                break;
            }
            case 's': {
                const char* s = va_arg(args, const char*);
                if (!s) s = "(null)";
                print(s);
                break;
            }
            case 'c': {
                putchar((char)va_arg(args, int));
                break;
            }
            case '%': putchar('%'); break;
            default:
                putchar('%');
                putchar(*fmt);
        }
        fmt++;
        written++;
    }
    return written;
}

int printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vprintf(fmt, args);
    va_end(args);
    return n;
}

int vsnprintf(char* buf, uint32_t size, const char* fmt, va_list args) {
    uint32_t pos = 0;
    #define PUT(c) do { if (pos + 1 < size) buf[pos++] = (c); } while (0)

    int iter = 0;
    while (*fmt) {

        if (*fmt != '%') {
            PUT(*fmt);
            fmt++;
            continue;
        }
        fmt++;

        int zero_pad = 0;
        if (*fmt == '0') { zero_pad = 1; fmt++; }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }

        int long_arg = 0;
        if (*fmt == 'l') { long_arg = 1; fmt++; if (*fmt == 'l') { long_arg = 2; fmt++; } }
        switch (*fmt) {
            case 'd': case 'i': {
                int64_t v = long_arg ? va_arg(args, int64_t) : va_arg(args, int);
                char tmp[24]; int n = 0;
                int neg = 0;
                if (v < 0) { neg = 1; v = -v; }
                if (v == 0) tmp[n++] = '0';
                while (v) { tmp[n++] = '0' + v % 10; v /= 10; }
                
                int total = n + neg;
                int pad = width - total;
                if (pad < 0) pad = 0;
                if (neg) PUT('-');
                while (pad-- > 0) PUT(zero_pad ? '0' : ' ');
                while (n--) PUT(tmp[n]);
                break;
            }
            case 'u': {
                uint64_t v = long_arg ? va_arg(args, uint64_t) : va_arg(args, uint32_t);
                char tmp[24]; int n = 0;
                if (v == 0) tmp[n++] = '0';
                while (v) { tmp[n++] = '0' + v % 10; v /= 10; }
                while (n--) PUT(tmp[n]);
                break;
            }
            case 'x': case 'X': {
                uint64_t v = long_arg ? va_arg(args, uint64_t) : va_arg(args, uint32_t);
                const char* hx = (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                char tmp[20]; int n = 0;
                if (v == 0) tmp[n++] = '0';
                while (v) { tmp[n++] = hx[v & 0xF]; v >>= 4; }
                while (n--) PUT(tmp[n]);
                break;
            }
            case 's': {
                const char* s = va_arg(args, const char*);
                if (!s) s = "(null)";
                while (*s) { PUT(*s); s++; }
                break;
            }
            case 'c': PUT((char)va_arg(args, int)); break;
            case '%': PUT('%'); break;
            default: PUT('%'); PUT(*fmt);
        }
        fmt++;
    }
    if (size > 0) buf[pos < size ? pos : size - 1] = '\0';
    #undef PUT
    return (int)pos;
}

int snprintf(char* buf, uint32_t size, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return n;
}


int getchar(void) {
    stdio_pick_primary();
    if (g_console && g_console->getchar) return g_console->getchar();
    return 0;
}

int kbhit(void) {
    stdio_pick_primary();
    if (g_console && g_console->kbhit) return g_console->kbhit();
    return 0;
}