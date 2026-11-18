#include "tss.h"
#include "gdt.h"

static TSS g_tss;

#define IST_STACK_SIZE (16 * 1024)
static uint8_t g_ist1_stack[IST_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t g_ist2_stack[IST_STACK_SIZE] __attribute__((aligned(16)));

void tss_init(void) {
    uint8_t* p = (uint8_t*)&g_tss;
    for (uint32_t i = 0; i < sizeof(g_tss); i++) p[i] = 0;

    g_tss.ist1 = (uint64_t)(uintptr_t)(g_ist1_stack + IST_STACK_SIZE);
    g_tss.ist2 = (uint64_t)(uintptr_t)(g_ist2_stack + IST_STACK_SIZE);

    g_tss.iopb_offset = sizeof(TSS);

    gdt_set_tss((uint64_t)(uintptr_t)&g_tss, sizeof(TSS) - 1);

    __asm__ volatile("ltr %0" : : "r"((uint16_t)GDT_TSS_SEL) : "memory");
}

void tss_set_rsp0(uint64_t rsp0) {
    g_tss.rsp0 = rsp0;
}

uint64_t tss_get_rsp0(void) {
    return g_tss.rsp0;
}

TSS* tss_get(void) {
    return &g_tss;
}