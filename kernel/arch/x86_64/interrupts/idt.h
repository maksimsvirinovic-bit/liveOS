#ifndef IDT_H
#define IDT_H

#include <stdint.h>

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  flags;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) IDTEntry;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) IDTPtr;

#define IDT_PRESENT   0x80
#define IDT_DPL0      0x00
#define IDT_DPL3      0x60
#define IDT_INTERRUPT 0x0E
#define IDT_TRAP      0x0F

#define IST_NONE  0
#define IST_DOUBLE_FAULT 1
#define IST_PAGE_FAULT   2

void idt_init(void);
void idt_set_gate(uint8_t vector, void* handler, uint16_t selector, uint8_t flags, uint8_t ist);

#endif