#ifndef EXCEPTION_H
#define EXCEPTION_H

#include <stdint.h>

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) ExceptionContext;

typedef enum {
    EXCEPTION_NOT_HANDLED = 0,
    EXCEPTION_HANDLED,
    EXCEPTION_HANDLED_SKIP,
} ExceptionResult;

typedef ExceptionResult (*ExceptionHandler)(ExceptionContext* ctx, void* user);

int exception_register_handler(uint8_t vector, ExceptionHandler handler, void* user);
int exception_unregister_handler(uint8_t vector);

void exceptions_init(void);
void exception_handler(ExceptionContext* ctx);

#endif