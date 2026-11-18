#include "pit.h"
#include "x86_64/io.h"
#include "x86_64/interrupts/irq.h"
#include "serial/serial.h"
#include <stddef.h>

#define PIT_CH0_DATA  0x40
#define PIT_CMD       0x43

static volatile uint32_t g_tick = 0;

static void pit_irq_handler(uint32_t irq, void* ctx) {
    (void)irq; (void)ctx;
    g_tick++;
}

void pit_init(void) {
    uint32_t divisor = PIT_BASE_FREQ / PIT_TARGET_HZ;

    outb(PIT_CMD, 0x36);

    outb(PIT_CH0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0_DATA, (uint8_t)((divisor >> 8) & 0xFF));

    irq_register(0, pit_irq_handler, NULL);

    serial_emergency_print("pit: init, target=");
    serial_emergency_print_hex(PIT_TARGET_HZ);
    serial_emergency_print(" Hz, divisor=");
    serial_emergency_print_hex(divisor);
    serial_emergency_print("\n");
}

uint32_t pit_get_tick(void) {
    return g_tick;
}

void pit_sleep_ticks(uint32_t ticks) {
    uint32_t target = g_tick + ticks;
    while (g_tick < target) {
        __asm__ volatile("hlt");
    }
}