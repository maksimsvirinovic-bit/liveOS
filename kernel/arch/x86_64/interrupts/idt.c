#include "idt.h"
#include <stdint.h>

static IDTEntry idt[256];
static IDTPtr idt_ptr;

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);

static void (*const stubs[32])(void) = {
    isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
    isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
    isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
};

#define KERNEL_CS 0x08

void idt_set_gate(uint8_t vector, void* handler, uint16_t selector, uint8_t flags, uint8_t ist) {
    uint64_t addr = (uint64_t)(uintptr_t)handler;
    idt[vector].offset_low  = (uint16_t)addr;
    idt[vector].selector    = selector;
    idt[vector].ist         = ist & 0x07;
    idt[vector].flags       = flags;
    idt[vector].offset_mid  = (uint16_t)(addr >> 16);
    idt[vector].offset_high = (uint32_t)(addr >> 32);
    idt[vector].reserved    = 0;
}

void idt_init(void) {
    uint8_t* p = (uint8_t*)idt;
    for (uint32_t i = 0; i < sizeof(idt); i++) p[i] = 0;

    for (int v = 0; v < 32; v++) {
        uint8_t ist = IST_NONE;

        if (v == 8)  ist = 1;
        if (v == 14) ist = 2;

        idt_set_gate(v, (void*)stubs[v], KERNEL_CS,
                     IDT_PRESENT | IDT_DPL0 | IDT_INTERRUPT, ist);
    }

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint64_t)(uintptr_t)idt;
    __asm__ volatile("lidt %0" : : "m"(idt_ptr) : "memory");
}