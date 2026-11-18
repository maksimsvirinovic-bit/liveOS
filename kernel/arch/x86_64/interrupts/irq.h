#ifndef IRQ_H
#define IRQ_H

#include "tasking/process.h"
#include <stdint.h>

#define IRQ_BASE_VECTOR  32
#define IRQ_COUNT        16

#define IRQ_TIMER    0
#define IRQ_KEYBOARD 1
#define IRQ_CASCADE  2
#define IRQ_COM2     3
#define IRQ_COM1     4
#define IRQ_LPT2     5
#define IRQ_FLOPPY   6
#define IRQ_LPT1     7
#define IRQ_RTC      8
#define IRQ_PCI1     9
#define IRQ_PCI2     10
#define IRQ_PCI3     11
#define IRQ_MOUSE    12
#define IRQ_FPU      13
#define IRQ_PRIMARY_ATA 14
#define IRQ_SECONDARY_ATA 15

typedef void (*IrqHandlerFn)(uint32_t irq, void* ctx);

void irq_init(void);

int  irq_register(uint32_t irq, IrqHandlerFn fn, void* ctx);
int  irq_unregister(uint32_t irq, IrqHandlerFn fn, void* ctx);

void irq_mask(uint32_t irq);
void irq_unmask(uint32_t irq);

uint64_t irq_dispatch(uint32_t irq, uint64_t old_rsp);

extern void irq_return(uint64_t);

#endif