#include "exceptions.h"
#include "idt.h"
#include <serial/serial.h>
#include <stddef.h>

typedef struct {
    ExceptionHandler handler;
    void* user;
} HandlerSlot;

static HandlerSlot handlers[32];

static const char* exception_names[32] = {
    "Divide Error", "Debug", "NMI", "Breakpoint", "Overflow",
    "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS",
    "Segment Not Present", "Stack-Segment Fault", "General Protection Fault",
    "Page Fault", "Reserved", "x87 FP Exception", "Alignment Check",
    "Machine Check", "SIMD FP Exception", "Virtualization Exception",
    "Control Protection", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Hypervisor Injection",
    "VMM Communication", "Security Exception", "Reserved",
};

int exception_register_handler(uint8_t vector, ExceptionHandler handler, void* user) {
    if (vector >= 32) return -1;
    handlers[vector].handler = handler;
    handlers[vector].user = user;
    return 0;
}

int exception_unregister_handler(uint8_t vector) {
    if (vector >= 32) return -1;
    handlers[vector].handler = NULL;
    handlers[vector].user = NULL;
    return 0;
}

__attribute__((noreturn))
static void kernel_halt(const char* reason) {
    serial_emergency_print("\n*** KERNEL HALT: ");
    serial_emergency_print(reason);
    serial_emergency_print(" ***\n");
    for (;;) __asm__ volatile("cli; hlt");
}

void exceptions_init(void) {
    for (int i = 0; i < 32; i++) {
        handlers[i].handler = NULL;
        handlers[i].user = NULL;
    }
    idt_init();
}

void exception_handler(ExceptionContext* ctx) {
    uint64_t vector = ctx->vector;
    uint64_t cs = ctx->cs;

    serial_force_print("exception: ");
    if (vector < 32) serial_force_print(exception_names[vector]);
    serial_force_print("\n");
    if (serial_debug_enabled()) {
        serial_force_print("vector="); serial_emergency_print_hex(vector);
        serial_force_print(" rip="); serial_emergency_print_hex(ctx->rip);
        serial_force_print(" cs="); serial_emergency_print_hex(cs);
        serial_force_print(" rflags="); serial_emergency_print_hex(ctx->rflags);
        serial_force_print(" rsp="); serial_emergency_print_hex(ctx->rsp);
        serial_force_print(" ss="); serial_emergency_print_hex(ctx->ss);
        serial_force_print(" error="); serial_emergency_print_hex(ctx->error_code);
        if (vector == 14) {
            uint64_t cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            serial_force_print(" cr2="); serial_emergency_print_hex(cr2);
        }
        serial_force_print("\n");
    }

    if (vector < 32 && handlers[vector].handler) {
        ExceptionResult result = handlers[vector].handler(ctx, handlers[vector].user);
        if (result == EXCEPTION_HANDLED || result == EXCEPTION_HANDLED_SKIP) {
            #ifdef DEBUG
            serial_emergency_print("Handler returned CONTINUE. Resuming...\n");
            #endif
            return;
        }
        if (result == EXCEPTION_NOT_HANDLED) {
            serial_emergency_print("Handler returned HALT.\n");
        }
    } else {
        serial_emergency_print("No handler registered for vector ");
        serial_emergency_print_hex(vector);
        serial_emergency_print(".\n");
    }

    if (vector == 3) {
        serial_emergency_print("Breakpoint interrupt. Recovering...\n");
        return;
    }

    kernel_halt("unrecoverable exception");
}