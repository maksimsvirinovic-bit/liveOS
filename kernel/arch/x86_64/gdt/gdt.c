#include "gdt.h"

#define GDT_ENTRIES 10

static GDTEntry g_gdt[GDT_ENTRIES];
static GDTPtr   g_gdt_ptr;

static void gdt_set_entry(int idx, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t gran) {
    g_gdt[idx].limit_low   = (uint16_t)(limit & 0xFFFF);
    g_gdt[idx].base_low    = (uint16_t)(base & 0xFFFF);
    g_gdt[idx].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    g_gdt[idx].access      = access;
    g_gdt[idx].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    g_gdt[idx].base_high   = (uint8_t)((base >> 24) & 0xFF);
}

void gdt_init(void) {
    gdt_set_entry(0, 0, 0, 0, 0);

    gdt_set_entry(1, 0, 0xFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 |
                  GDT_ACCESS_S | GDT_ACCESS_EX | GDT_ACCESS_RW,
                  GDT_GRAN_4KB | GDT_GRAN_64BIT);

    gdt_set_entry(2, 0, 0xFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 |
                  GDT_ACCESS_S | GDT_ACCESS_RW,
                  GDT_GRAN_4KB);

    gdt_set_entry(3, 0, 0xFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 |
                  GDT_ACCESS_S | GDT_ACCESS_RW,
                  GDT_GRAN_4KB);

    gdt_set_entry(4, 0, 0xFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 |
                  GDT_ACCESS_S | GDT_ACCESS_EX | GDT_ACCESS_RW,
                  GDT_GRAN_4KB | GDT_GRAN_64BIT);

    g_gdt[5].limit_low   = 0;
    g_gdt[5].base_low    = 0;
    g_gdt[5].base_mid    = 0;
    g_gdt[5].access      = 0;
    g_gdt[5].granularity = 0;
    g_gdt[5].base_high   = 0;

    g_gdt_ptr.limit = sizeof(g_gdt) - 1;
    g_gdt_ptr.base  = (uint64_t)(uintptr_t)&g_gdt[0];
    __asm__ volatile("lgdt %0" : : "m"(g_gdt_ptr) : "memory");

        __asm__ volatile(
        "pushq %[kcs]\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw %[kds], %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        :
        : [kcs] "i"(GDT_KERNEL_CODE_SEL),
          [kds] "i"(GDT_KERNEL_DATA_SEL)
        : "rax", "memory"
    );
}

void gdt_set_tss(uint64_t tss_base, uint32_t tss_limit) {
    GDTEntryTSS* e = (GDTEntryTSS*)&g_gdt[5];
    e->limit_low   = (uint16_t)(tss_limit & 0xFFFF);
    e->base_low    = (uint16_t)(tss_base & 0xFFFF);
    e->base_mid    = (uint8_t)((tss_base >> 16) & 0xFF);
    e->access      = 0x89;
    e->granularity = (uint8_t)((tss_limit >> 16) & 0x0F);
    e->base_high   = (uint8_t)((tss_base >> 24) & 0xFF);
    e->base_high32 = (uint32_t)((tss_base >> 32) & 0xFFFFFFFF);
    e->reserved    = 0;

    __asm__ volatile("lgdt %0" : : "m"(g_gdt_ptr) : "memory");
}