#include "vmalloc.h"
#include "spinlock.h"
#include "paging.h"
#include "pmm.h"
#include "heap.h"
#include <serial/serial.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct VmallocRegion {
    uint64_t start;
    uint64_t end;
    int      free;
    bool     is_mmio;
    struct VmallocRegion* next;
} VmallocRegion;

static VmallocRegion* g_regions = NULL;
static uint64_t       g_vmalloc_used = 0;

static VmallocRegion* region_alloc(uint64_t start, uint64_t end, int free_flag) {
    VmallocRegion* r = (VmallocRegion*)kmalloc(sizeof(VmallocRegion));
    if (!r) return NULL;
    r->start = start;
    r->end = end;
    r->free = free_flag;
    r->is_mmio = false;
    r->next = NULL;
    return r;
}

static void region_insert(VmallocRegion* r) {
    VmallocRegion** pp = &g_regions;
    while (*pp && (*pp)->start < r->start) pp = &(*pp)->next;
    r->next = *pp;
    *pp = r;
}

static void region_merge(void) {
    VmallocRegion* r = g_regions;
    while (r && r->next) {
        if (r->free && r->next->free && r->end == r->next->start) {
            VmallocRegion* dead = r->next;
            r->end = dead->end;
            r->next = dead->next;
            kfree(dead);
        } else {
            r = r->next;
        }
    }
}

void vmalloc_init(void) {
    spinlock_t* sl = kmalloc(sizeof(spinlock_t));
    spin_lock_irqsave(sl);
    g_regions = NULL;
    g_vmalloc_used = 0;

    VmallocRegion* r = region_alloc(VMALLOC_BASE, VMALLOC_END, 1);
    if (!r) {
        serial_emergency_print("vmalloc: init failed\n");
        return;
    }
    g_regions = r;

    serial_emergency_print("vmalloc: init [");
    serial_emergency_print_hex(VMALLOC_BASE);
    serial_emergency_print("..");
    serial_emergency_print_hex(VMALLOC_END);
    serial_emergency_print(")\n");
    spin_unlock_irqrestore(sl);
    kfree(sl);
}

static uint64_t align_up(uint64_t v, uint64_t a) {
    return (v + a - 1) & ~(a - 1);
}

uint64_t vmalloc_reserve_mmio(uint64_t size) {
    uint64_t virt = vmalloc_reserve(size);
    if (!virt){
        return 0;
    }
    for (VmallocRegion* r = g_regions; r; r = r->next) {
        if (r->start == virt && !r->free) {
            r->is_mmio = true;
            break;
        }
    }
    return virt;
}

uint64_t vmalloc_reserve(uint64_t size) {
    if (size == 0) return 0;
    size = align_up(size, PAGE_SIZE);

    for (VmallocRegion* r = g_regions; r; r = r->next) {
        if (!r->free) continue;
        uint64_t rsize = r->end - r->start;
        if (rsize < size) continue;

        uint64_t result = r->start;
        if (rsize == size) {
            r->free = 0;
        } else {
            VmallocRegion* used = region_alloc(result, result + size, 0);
            if (!used) {
                return 0;
            }
            r->start = result + size;
            used->next = r;
            VmallocRegion** pp = &g_regions;
            while (*pp && *pp != r) pp = &(*pp)->next;
            *pp = used;
        }

        g_vmalloc_used += size;
        return result;
    }

    serial_emergency_print("vmalloc: out of virtual space\n");
    return 0;
}

void* vmalloc_page(void) {
    spinlock_t* sl = kmalloc(sizeof(spinlock_t));
    spin_lock_irqsave(sl);
    uint64_t virt = vmalloc_reserve(PAGE_SIZE);
    if (!virt){
        spin_unlock_irqrestore(sl);
        kfree(sl);
        return NULL;
    }

    uint64_t phys = pmm_alloc_page();
    if (!phys) {
        vfree((void*)(uintptr_t)virt);
        spin_unlock_irqrestore(sl);
        kfree(sl);
        return NULL;
    }

    if (paging_map_4k(paging_kernel_pml4_phys(), phys, virt,
                      PAGE_PRESENT | PAGE_WRITE) != 0) {
        pmm_free_page(phys);
        vfree((void*)(uintptr_t)virt);
        spin_unlock_irqrestore(sl);
        kfree(sl);
        return NULL;
    }

    spin_unlock_irqrestore(sl);
    kfree(sl);
    return (void*)(uintptr_t)virt;
}

uint64_t vmalloc_kernel_stack(void) {
    uint64_t phys = pmm_alloc_pages((uint32_t)(KERNEL_STACK_SIZE / PAGE_SIZE));
    if (!phys){
        return 0;
    }
    uint64_t base = (uint64_t)(uintptr_t)phys_to_virt(phys);

    serial_emergency_print("vmalloc: kernel stack [");
    serial_emergency_print_hex(base);
    serial_emergency_print("..");
    serial_emergency_print_hex(base + KERNEL_STACK_SIZE);
    serial_emergency_print(")\n");
    return base;
}

void vfree(void* ptr) {
    if (!ptr) return;
    spinlock_t* sl = kmalloc(sizeof(spinlock_t));
    spin_lock_irqsave(sl);
    uint64_t addr = (uint64_t)(uintptr_t)ptr;

    VmallocRegion* r = g_regions;
    while (r) {
        if (r->start == addr && !r->free) break;
        r = r->next;
    }
    if (!r) {
        spin_unlock_irqrestore(sl);
        kfree(sl);
        serial_emergency_print("vfree: unknown region\n");
        return;
    }

    serial_emergency_print("vmalloc: free [");
    serial_emergency_print_hex(r->start);
    serial_emergency_print("..");
    serial_emergency_print_hex(r->end);
    serial_emergency_print(")\n");

    for (uint64_t v = r->start; v < r->end; v += PAGE_SIZE) {
        uint64_t phys = paging_get_phys(paging_kernel_pml4_phys(), v);
        if (phys) {
            paging_unmap_4k(paging_kernel_pml4_phys(), v);
            if (!r->is_mmio) {
                pmm_free_page(phys);
            }
        }
    }

    g_vmalloc_used -= (r->end - r->start);
    r->free = 1;
    region_merge();
    spin_unlock_irqrestore(sl);
    kfree(sl);
}

void vfree_kernel_stack(uint64_t base) {
    spinlock_t* sl = kmalloc(sizeof(spinlock_t));
    spin_lock_irqsave(sl);
    if (base) {
        uint64_t phys = virt_to_phys(base);
        pmm_free_pages(phys, (uint32_t)(KERNEL_STACK_SIZE / PAGE_SIZE));
    }
    spin_unlock_irqrestore(sl);
    kfree(sl);
}

uint64_t vmalloc_used(void)  { return g_vmalloc_used; }
uint64_t vmalloc_total(void) { return VMALLOC_SIZE; }

void vmalloc_dump(void) {
    serial_emergency_print("vmalloc regions:\n");
    for (VmallocRegion* r = g_regions; r; r = r->next) {
        serial_emergency_print("  [");
        serial_emergency_print_hex(r->start);
        serial_emergency_print("..");
        serial_emergency_print_hex(r->end);
        serial_emergency_print(r->free ? "] FREE\n" : "] used\n");
    }
}