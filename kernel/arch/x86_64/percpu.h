#ifndef PERCPU_H
#define PERCPU_H

#include <stdint.h>

#define MAX_CPUS 64

typedef struct PerCpu {
    uint64_t self;

    uint32_t cpu_id;
    uint32_t apic_id;

    uint64_t kstack_top;
    uint64_t user_rsp_save;

    void*    current_process;
    void*    current_as;

    uint64_t syscall_count;
    uint64_t irq_count;

    uint8_t  padding[64];
} __attribute__((aligned(64))) PerCpu;

static inline PerCpu* this_cpu(void) {
    PerCpu* p;
    __asm__ volatile("mov %%gs:0, %0" : "=r"(p));
    return p;
}

int  percpu_init_bsp(void);
void percpu_set_cpu_count(uint32_t count);
uint32_t percpu_cpu_count(void);
PerCpu* percpu_get(uint32_t cpu_id);

#endif