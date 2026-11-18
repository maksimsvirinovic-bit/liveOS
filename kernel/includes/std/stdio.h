#ifndef _STDIO_H
#define _STDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdarg.h>

void stdio_init(void);

void putchar(char c);
void print(const char* s);
void puts(const char* s);
void print_hex(uint64_t v);
void print_dec(uint64_t v);

int  printf(const char* fmt, ...);
int  vprintf(const char* fmt, va_list args);
int  snprintf(char* buf, uint32_t size, const char* fmt, ...);
int  vsnprintf(char* buf, uint32_t size, const char* fmt, va_list args);

int  getchar(void);
int  kbhit(void);

#define STDIO_KEY_UP      (-10)
#define STDIO_KEY_DOWN    (-11)
#define STDIO_KEY_LEFT    (-12)
#define STDIO_KEY_RIGHT   (-13)
#define STDIO_KEY_HOME    (-14)
#define STDIO_KEY_END     (-15)
#define STDIO_KEY_PGUP    (-16)
#define STDIO_KEY_PGDN    (-17)
#define STDIO_KEY_DELETE  (-18)
#define STDIO_KEY_INSERT  (-19)

void stdio_set_color(uint32_t fg, uint32_t bg);
void stdio_reset_color(void);

#ifdef __cplusplus
}
#endif

#endif