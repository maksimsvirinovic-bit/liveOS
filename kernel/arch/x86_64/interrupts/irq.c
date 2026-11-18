#include "irq.h"
#include <x86_64/io.h>
#include "idt.h"
#include <serial/serial.h>
#include <stddef.h>
#include <heap.h>
#include <tasking/scheduler.h>

#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1

#define PIC_EOI    0x20

typedef struct IrqHandlerEntry {
    IrqHandlerFn fn;
    void* ctx;
    struct IrqHandlerEntry* next;
} IrqHandlerEntry;

static IrqHandlerEntry* g_handlers[IRQ_COUNT];
static int g_irq_count[IRQ_COUNT];


static void pic_remap(void) {
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);
    outb(PIC1_DATA, IRQ_BASE_VECTOR);
    outb(PIC2_DATA, IRQ_BASE_VECTOR + 8);
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

static void pic_send_eoi(uint32_t irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

static void pic_mask(uint32_t irq, int masked) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = (uint8_t)(irq & 7);
    uint8_t val = inb(port);
    if (masked) val |= (1u << bit);
    else        val &= ~(1u << bit);
    outb(port, val);
}


extern void irq0(void);  extern void irq1(void);  extern void irq2(void);  extern void irq3(void);
extern void irq4(void);  extern void irq5(void);  extern void irq6(void);  extern void irq7(void);
extern void irq8(void);  extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void); extern void irq15(void);

static void (*const irq_stubs[IRQ_COUNT])(void) = {
    irq0,  irq1,  irq2,  irq3,  irq4,  irq5,  irq6,  irq7,
    irq8,  irq9,  irq10, irq11, irq12, irq13, irq14, irq15,
};


void irq_init(void) {
    for (int i = 0; i < IRQ_COUNT; i++) {
        g_handlers[i] = NULL;
        g_irq_count[i] = 0;
    }

    pic_remap();

    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    for (int i = 0; i < IRQ_COUNT; i++) {
        idt_set_gate((uint8_t)(IRQ_BASE_VECTOR + i),
                     (void*)irq_stubs[i],
                     0x08,
                     0x8E,
                     0);
    }

    serial_emergency_print("irq: PIC remapped to 0x20-0x2F, IDT entries installed\n");
}


int irq_register(uint32_t irq, IrqHandlerFn fn, void* ctx) {
    if (irq >= IRQ_COUNT || !fn) return -1;

    IrqHandlerEntry* e = (IrqHandlerEntry*)kmalloc(sizeof(IrqHandlerEntry));
    if (!e) return -1;
    e->fn = fn;
    e->ctx = ctx;
    e->next = g_handlers[irq];
    g_handlers[irq] = e;

    irq_unmask(irq);
    return 0;
}

int irq_unregister(uint32_t irq, IrqHandlerFn fn, void* ctx) {
    if (irq >= IRQ_COUNT) return -1;

    IrqHandlerEntry** pp = &g_handlers[irq];
    while (*pp) {
        if ((*pp)->fn == fn && (*pp)->ctx == ctx) {
            IrqHandlerEntry* dead = *pp;
            *pp = dead->next;
            kfree(dead);
            return 0;
        }
        pp = &(*pp)->next;
    }
    return -1;
}


void irq_mask(uint32_t irq) {
    if (irq < IRQ_COUNT) pic_mask(irq, 1);
}

void irq_unmask(uint32_t irq) {
    if (irq < IRQ_COUNT) pic_mask(irq, 0);
    if (irq >= 8 && irq < IRQ_COUNT) {
        pic_mask(2, 0);
    }
}


uint64_t irq_dispatch(uint32_t irq, uint64_t old_rsp) {
    if (irq >= IRQ_COUNT) {
        pic_send_eoi(irq);
        return old_rsp;
    }

    g_irq_count[irq]++;

    for (IrqHandlerEntry* e = g_handlers[irq]; e; e = e->next) {
        e->fn(irq, e->ctx);
    }

    pic_send_eoi(irq);

    if (irq == 0) {
        return scheduler_tick(old_rsp);
    }

    return old_rsp;
}