#ifndef PANIC_H
#define PANIC_H

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((noreturn))
void panic(const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#endif