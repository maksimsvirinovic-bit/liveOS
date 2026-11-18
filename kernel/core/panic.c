#include "panic.h"
#include <stdio.h>
#include <stdarg.h>
#include <x86_64/io.h>

__attribute__((noreturn))
void panic(const char* fmt, ...) {
    printf("\n*** KERNEL PANIC ***\n");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    for (;;) {
        cli();
        hlt();
    }
}