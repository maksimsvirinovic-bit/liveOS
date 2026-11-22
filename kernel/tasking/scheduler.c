#include "scheduler.h"
#include "process.h"
#include "paging.h"
#include "x86_64/gdt/tss.h"
#include "x86_64/percpu.h"
#include "serial/serial.h"
#include <stddef.h>

#define MIN_GRANULARITY  1
#define VRUNTIME_SHIFT   20
#define VRUNTIME_ONE     (1ULL << VRUNTIME_SHIFT)

static Process* g_ready_queue = NULL;
static Process* g_idle        = NULL;
static uint64_t g_min_vruntime = 0;
static uint64_t g_tick_counter = 0;
static volatile int g_force_switch = 0;
static void idle_loop(void) {
    for (;;) {
        __asm__ volatile("sti; hlt");
    }
}

Process* scheduler_pick_next_excluding(Process* exclude) {
    Process* best = NULL;
    uint64_t best_vruntime = ~0ULL;
    for (Process* p = g_ready_queue; p; p = p->run_next) {
        if (p->state != PROC_READY) continue;
        if (p == exclude) continue;
        if (p->vruntime < best_vruntime) {
            best_vruntime = p->vruntime;
            best = p;
        }
    }
    return best;
}
void scheduler_init(void) {
    g_ready_queue = NULL;
    g_idle = NULL;
    g_min_vruntime = 0;
    g_tick_counter = 0;

    g_idle = process_create(0, NULL, "idle");
    if (g_idle) {
        g_idle->pid = 0;
        g_idle->vruntime = 0;
        g_idle->preemptible = 1;
        g_idle->weight = NICE_0_WEIGHT;
        process_setup_initial_frame(g_idle, (uint64_t)idle_loop,
                                      g_idle->kstack_top, 0x08, 0x10, 0x202);
    }
}

void scheduler_add(Process* p) {
    if (!p) return;
    if (p->vruntime == 0) {
        p->vruntime = g_min_vruntime;
    }
    p->last_run = g_tick_counter;
    p->run_next = g_ready_queue;
    g_ready_queue = p;
    if (p->vruntime < g_min_vruntime) {
        g_min_vruntime = p->vruntime;
    }
    p->state = PROC_READY;
}

void scheduler_remove(Process* p) {
    if (!p) return;
    Process** pp = &g_ready_queue;
    while (*pp) {
        if (*pp == p) { *pp = p->run_next; p->run_next = NULL; return; }
        pp = &(*pp)->run_next;
    }
}
Process* scheduler_pick_next(void) {
    Process* best = NULL;
    uint64_t best_vruntime = ~0ULL;

    for (Process* p = g_ready_queue; p; p = p->run_next) {
        if (p->state != PROC_READY) continue;
        if (p->vruntime < best_vruntime) {
            best_vruntime = p->vruntime;
            best = p;
        }
    }
    return best ? best : g_idle;
}
static uint64_t scheduler_switch(uint64_t old_rsp) {
    Process* current = process_get_current();
    Process* next = scheduler_pick_next();

    if (!next || next == current) return old_rsp;

    if (current) current->saved_rsp = old_rsp;

    if (next->as) paging_switch(next->as->pml4_phys);
    if (next->kstack_top) {
        tss_set_rsp0(next->kstack_top);
        percpu_get(0)->kstack_top = next->kstack_top;
    }

    process_set_current(next);
    next->last_run = g_tick_counter;

    return next->saved_rsp;
}

static uint64_t scheduler_switch_to(uint64_t old_rsp, Process* next) {
    Process* current = process_get_current();
    if (!next || next == current) return old_rsp;

    if (current) current->saved_rsp = old_rsp;

    if (next->as) paging_switch(next->as->pml4_phys);
    if (next->kstack_top) {
        tss_set_rsp0(next->kstack_top);
        percpu_get(0)->kstack_top = next->kstack_top;
    }
    process_set_current(next);
    next->last_run = g_tick_counter;

    return next->saved_rsp;
}

#define VRUNTIME_SHIFT  20
#define VRUNTIME_ONE    (1ULL << VRUNTIME_SHIFT)

uint64_t scheduler_tick(uint64_t old_rsp) {
    g_tick_counter++;

    Process* current = process_get_current();
    if (!current) return old_rsp;

    if (current->state == PROC_SLEEPING) {
        Process* next = scheduler_pick_next_excluding(current);
        if (!next) next = g_idle;
        if (!next || next == current) return old_rsp;
        return scheduler_switch_to(old_rsp, next);
    }

    uint64_t delta = g_tick_counter - current->last_run;
    if (delta == 0) delta = 1;
    current->vruntime += ((uint64_t)delta * NICE_0_WEIGHT * VRUNTIME_ONE) / current->weight;
    current->last_run = g_tick_counter;
    if (current->vruntime < g_min_vruntime) g_min_vruntime = current->vruntime;

    if (!current->preemptible && !g_force_switch) {
        current->need_resched = 1;
        return old_rsp;
    }

    Process* next = scheduler_pick_next_excluding(current);
    if (!next) next = g_idle;
    if (!next || next == current) return old_rsp;
    return scheduler_switch_to(old_rsp, next);
}

void scheduler_yield(void) {
    g_force_switch = 1;
    __asm__ volatile("int $0x20");
    g_force_switch = 0;
}
void scheduler_exit_current(void) {
    Process* current = process_get_current();
    if (current) {
        scheduler_remove(current);
        current->state = PROC_ZOMBIE;
    }

    Process* next = scheduler_pick_next();
    if (!next) next = g_idle;
    if (!next) {
        serial_emergency_print("scheduler: no processes\n");
        for (;;) __asm__ volatile("cli; hlt");
    }

    if (next->as) paging_switch(next->as->pml4_phys);
    if (next->kstack_top) {
        tss_set_rsp0(next->kstack_top);
        percpu_get(0)->kstack_top = next->kstack_top;
    }
    process_set_current(next);
    next->last_run = g_tick_counter;

    extern void irq_return(uint64_t rsp);
    irq_return(next->saved_rsp);
    __builtin_unreachable();
}
void scheduler_start(void) {
    Process* first = scheduler_pick_next();
    if (!first) first = g_idle;
    if (!first) return;

    if (first->as) paging_switch(first->as->pml4_phys);
    if (first->kstack_top) {
        tss_set_rsp0(first->kstack_top);
        percpu_get(0)->kstack_top = first->kstack_top;
    }
    process_set_current(first);
    first->last_run = g_tick_counter;

    extern void irq_return(uint64_t rsp);
    irq_return(first->saved_rsp);
    __builtin_unreachable();
}

void scheduler_sleep(void) {
    Process* current = process_get_current();
    if (!current || current == g_idle) return;
    current->state = PROC_SLEEPING;
    scheduler_remove(current);
    scheduler_yield();   
}

void scheduler_wakeup(Process* p) {
    if (!p) return;
    if (p->state != PROC_SLEEPING) {
        return;
    }
    p->state = PROC_READY;
    scheduler_add(p);
}