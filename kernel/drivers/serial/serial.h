#ifndef SERIAL_H
#define SERIAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ops/driver.h"

void serial_driver_register(void);

void serial_emergency_init(void);
void serial_force_putc(char c);
void serial_force_print(const char* text);
void serial_emergency_putc(char c);
void serial_emergency_print(const char* s);
void serial_emergency_print_hex(uint64_t v);
void serial_debug_putc(char c);
void serial_debug_print(const char* text);
void serial_set_debug_enabled(int enabled);
int serial_debug_enabled(void);

#ifdef __cplusplus
}
#endif

#endif