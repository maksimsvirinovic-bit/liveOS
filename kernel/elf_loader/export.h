#ifndef EXPORT_H
#define EXPORT_H

#include <stdint.h>

#ifndef SYM_KERNEL
#define SYM_KERNEL    (1 << 0)
#define SYM_USER      (1 << 1)
#define SYM_FUNC      (1 << 2)
#define SYM_DATA      (1 << 3)
#define SYM_EXPORTED  (1 << 4)
#define SYM_WEAK      (1 << 5)
#endif

#define EXPORT_SYMBOL(name) \
    __attribute__((section(".exp"), used, aligned(8))) \
    static ElfDynSymbol __exp_##name = { \
        .name = #name, \
        .addr = (uint64_t)&name, \
        .ptr = 0, \
        .flags = SYM_FUNC | SYM_EXPORTED, \
        .version = 1, \
        .reserved = 0, \
    }

#define IMPORT_SYMBOL(name) \
    static void* __imp_ptr_##name = 0; \
    __attribute__((naked, used, noinline)) \
    void name(void) { \
        __asm__ volatile( \
            "jmp *__imp_ptr_" #name "(%rip)\n\t" \
            "nop\n\t" \
        ); \
    } \
    __attribute__((section(".imp"), used, aligned(8))) \
    static ElfDynSymbol __imp_sym_##name = { \
        .name = #name, \
        .addr = 0, \
        .ptr = &__imp_ptr_##name, \
        .flags = SYM_FUNC, \
        .version = 1, \
        .reserved = 0, \
    }

#endif