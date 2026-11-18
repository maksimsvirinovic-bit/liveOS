#include "percpu.h"
#include "msr.h"
#include <serial/serial.h>
#include <stddef.h>

static PerCpu g_percpu[MAX_CPUS];
static uint32_t g_cpu_count = 0;

void percpu_set_cpu_count(uint32_t count) {
    if (count > MAX_CPUS) count = MAX_CPUS;
    g_cpu_count = count;
}

uint32_t percpu_cpu_count(void) {
    return g_cpu_count;
}

PerCpu* percpu_get(uint32_t cpu_id) {
    if (cpu_id >= MAX_CPUS) return NULL;
    return &g_percpu[cpu_id];
}

int percpu_init_bsp(void) {
    PerCpu* p = &g_percpu[0];
    p->self = (uint64_t)(uintptr_t)p;
    p->cpu_id = 0;
    p->apic_id = 0;
    p->kstack_top = 0;
    p->user_rsp_save = 0;
    p->current_process = NULL;
    p->current_as = NULL;
    p->syscall_count = 0;
    p->irq_count = 0;

    wrmsr(MSR_GS_BASE, (uint64_t)(uintptr_t)p);
    wrmsr(MSR_KERNEL_GS, 0);

    percpu_set_cpu_count(1);

    serial_emergency_print("percpu: BSP @");
    serial_emergency_print_hex((uint64_t)(uintptr_t)p);
    serial_emergency_print("\n");
    return 0;
}