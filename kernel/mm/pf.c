#include "pf.h"
#include "vmm.h"
#include "pmm.h"
#include "paging.h"
#include "tasking/process.h"
#include "graphics/framebuffer.h"
#include "panic.h"
#include "serial/serial.h"
#include <stddef.h>

#define PF_PRESENT  (1u << 0)
#define PF_WRITE    (1u << 1)
#define PF_USER     (1u << 2)
#define PF_RSVD     (1u << 3)
#define PF_INSTR    (1u << 4)

static int is_kernel_stack_guard(uint64_t addr) {
    Process* p = process_get_current();
    if (!p || !p->kstack_base) return 0;
    return addr >= p->kstack_guard && addr < p->kstack_guard + PAGE_SIZE;
}

static int is_user_stack_guard(AddressSpace* as, uint64_t addr) {
    if (addr >= USER_STACK_LIMIT - PAGE_SIZE && addr < USER_STACK_LIMIT) return 1;
    (void)as;
    return 0;
}

static int is_kernel_heap_addr(uint64_t addr) {
    return addr >= KERNEL_HEAP_BASE && addr < KERNEL_HEAP_BASE + KERNEL_HEAP_SIZE;
}

static int resolve_cow_fault(Process* proc, uint64_t addr) {
    VMA* vma = vma_find(&proc->as->vmas, addr);
    if (!vma || !(vma->flags & VMA_FLAG_COW) || !(vma->flags & PAGE_WRITE)) return 0;

    uint64_t page_start = addr & ~(PAGE_SIZE - 1);
    uint64_t old_phys = paging_get_phys(proc->as->pml4_phys, page_start);
    if (!old_phys) return -1;

    uint64_t new_phys = pmm_alloc_page();
    if (!new_phys) return -1;

    uint8_t* src = (uint8_t*)phys_to_virt(old_phys);
    uint8_t* dst = (uint8_t*)phys_to_virt(new_phys);
    for (uint64_t i = 0; i < PAGE_SIZE; i++) dst[i] = src[i];

    pmm_unref_page(old_phys);

    uint64_t pte_flags = (vma->flags & ~VMA_FLAG_COW) | PAGE_WRITE | PAGE_PRESENT | PAGE_USER;
    if (paging_map_4k(proc->as->pml4_phys, new_phys, page_start, pte_flags) != 0) {
        pmm_free_page(new_phys);
        return -1;
    }

    __asm__ volatile("invlpg (%0)" : : "r"(page_start) : "memory");
    if (!paging_user_access(proc->as->pml4_phys, addr, 1)) {
        serial_emergency_print("pf: COW remap is not writable at ");
        serial_emergency_print_hex(addr);
        serial_emergency_print("\n");
        return -1;
    }
    return 1;
}

ExceptionResult pf_handler(ExceptionContext* ctx, void* user) {
    (void)user;

    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    uint64_t err = ctx->error_code;
    int present = (err & PF_PRESENT) != 0;
    int write   = (err & PF_WRITE) != 0;
    int from_user = (err & PF_USER) != 0;
    int instr   = (err & PF_INSTR) != 0;
    #ifdef DEBUG
    serial_emergency_print("pf: addr=");
    serial_emergency_print_hex(cr2);
    serial_emergency_print(" err=");
    serial_emergency_print_hex(err);
    serial_emergency_print("\n");
    #endif

    if (is_kernel_stack_guard(cr2)) {
        serial_emergency_print("pf: KERNEL STACK OVERFLOW!\n");
        panic("kernel stack overflow");
    }

    Process* proc = process_get_current();
    if (present && write && proc && proc->as) {
        VMA* vma = vma_find(&proc->as->vmas, cr2);
        int cow_result = resolve_cow_fault(proc, cr2);
        if (cow_result > 0) return EXCEPTION_HANDLED;
        if (cow_result < 0 && vma && (vma->flags & VMA_FLAG_COW)) {
            if (from_user) {
                sys_exit_impl(-12);
                return EXCEPTION_HANDLED;
            }
            panic("kernel COW fault resolution failed");
        }
    }

    if (!from_user) {
        if (is_kernel_heap_addr(cr2)) {
            serial_emergency_print("pf: kernel heap fault\n");
            panic("kernel heap fault");
        }
        serial_emergency_print("pf: kernel fault at ");
        serial_emergency_print_hex(cr2);
        serial_emergency_print(" rip=");
        serial_emergency_print_hex(ctx->rip);
        serial_emergency_print(" write_target=");
        serial_emergency_print_hex(ctx->rdx);
        serial_emergency_print(" rsp=");
        serial_emergency_print_hex(ctx->rsp);
        serial_emergency_print(" err=");
        serial_emergency_print_hex(err);
        serial_emergency_print(" cr3=");
        serial_emergency_print_hex(paging_get_cr3());
        serial_emergency_print(" kernel_phys=");
        serial_emergency_print_hex(paging_get_phys(paging_kernel_pml4_phys(), cr2));
        if (proc && proc->as) {
            serial_emergency_print(" process_phys=");
            serial_emergency_print_hex(paging_get_phys(proc->as->pml4_phys, cr2));
        }
            Framebuffer* fb = fb_get();
            if (fb) {
                serial_emergency_print(" fb_pixels=");
                serial_emergency_print_hex((uint64_t)(uintptr_t)fb->pixels);
                serial_emergency_print(" fb_bytes=");
                serial_emergency_print_hex((uint64_t)fb->pitch_pixels * fb->height * sizeof(uint32_t));
            }
        serial_emergency_print("\n");
        panic("kernel page fault");
    }

    if (!proc || !proc->as) {
        panic("user #PF without process");
    }

    if (is_user_stack_guard(proc->as, cr2)) {
        serial_emergency_print("pf: user stack overflow\n");
        sys_exit_impl(-11);
        return EXCEPTION_HANDLED;
    }

    if (present && write) {
        VMA* vma = vma_find(&proc->as->vmas, cr2);
        if (!vma) {
            serial_emergency_print("pf: segfault (no VMA)\n");
            sys_exit_impl(-11);
            return EXCEPTION_HANDLED;
        }

        serial_emergency_print("pf: write to RO VMA\n");
        sys_exit_impl(-11);
        return EXCEPTION_HANDLED;
    }

    if (!present) {
        VMA* vma = vma_find(&proc->as->vmas, cr2);
        if (vma) {
            serial_emergency_print("pf: lazy alloc not implemented\n");
            sys_exit_impl(-11);
            return EXCEPTION_HANDLED;
        }

        serial_emergency_print("pf: segfault (not present, no VMA)\n");
        sys_exit_impl(-11);
        return EXCEPTION_HANDLED;
    }

    if (instr) {
        serial_emergency_print("pf: instruction fetch fault\n");
        sys_exit_impl(-11);
        return EXCEPTION_HANDLED;
    }

    serial_emergency_print("pf: unhandled\n");
    panic("unhandled page fault");
}

void pf_init(void) {
    if (exception_register_handler(14, pf_handler, NULL) != 0) {
        serial_emergency_print("pf: register handler failed\n");
    } else {
        serial_emergency_print("pf: handler registered\n");
    }
}