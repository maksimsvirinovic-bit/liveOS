#include "vmm.h"
#include "paging.h"
#include "pmm.h"
#include "heap.h"
#include "vmalloc.h"
#include "user_abi.h"
#include <serial/serial.h>
#include <stddef.h>
#include <x86_64/gdt/tss.h>
#include <x86_64/syscall.h>
#include <x86_64/percpu.h>
#include <stdio.h>

static AddressSpace g_kernel_as;
static int          g_kernel_as_valid = 0;
static AddressSpace* g_current = NULL;

extern int  vmm_enter_user_asm(AddressSpace* as, uint64_t rip, uint64_t rsp);
extern void vmm_yield_asm(AddressSpace* as, uint64_t rip, uint64_t rsp, uint64_t rflags);

int vmm_init(uint64_t kernel_pml4_phys) {
    if (!kernel_pml4_phys) return -1;

    g_kernel_as.pml4_phys        = kernel_pml4_phys;
    g_kernel_as.kernel_pml4_phys = kernel_pml4_phys;
    vma_list_init(&g_kernel_as.vmas);
    g_kernel_as.brk_start = 0;
    g_kernel_as.brk_end = 0;
    g_kernel_as.kstack_phys = 0;
    g_kernel_as.kstack_top = 0;
    g_kernel_as.next = NULL;

    g_kernel_as_valid = 1;
    g_current = &g_kernel_as;

    printf("vmm: kernel AS pml4=%x\n", kernel_pml4_phys);
    return 0;
}

AddressSpace* vmm_kernel(void) {
    return g_kernel_as_valid ? &g_kernel_as : NULL;
}

AddressSpace* vmm_current(void) {
    return g_current;
}

AddressSpace* vmm_create(void) {
    if (!g_kernel_as_valid) {
        print("vmm_create: no kernel AS\n");
        return NULL;
    }

    AddressSpace* as = (AddressSpace*)kmalloc(sizeof(AddressSpace));
    if (!as) return NULL;

    uint64_t pml4_phys = pmm_alloc_page();
    if (!pml4_phys) {
        kfree(as);
        return NULL;
    }

    uint64_t* new_pml4 = (uint64_t*)phys_to_virt(pml4_phys);
    uint64_t* k_pml4   = (uint64_t*)phys_to_virt(g_kernel_as.pml4_phys);

    for (int i = 0; i < 256; i++)     new_pml4[i] = 0;
    for (int i = 256; i < 512; i++)   new_pml4[i] = k_pml4[i];

    as->pml4_phys = pml4_phys;
    as->kernel_pml4_phys = g_kernel_as.pml4_phys;
    vma_list_init(&as->vmas);
    as->brk_start = USER_HEAP_BASE;
    as->brk_end   = USER_HEAP_BASE;
    as->kstack_phys = 0;
    as->kstack_top = 0;
    as->next = NULL;

    return as;
}

static void free_user_page_tables(uint64_t pml4_phys) {
    uint64_t* pml4 = (uint64_t*)phys_to_virt(pml4_phys);

    for (int i = 0; i < 256; i++) {
        uint64_t e = pml4[i];
        if (!(e & PAGE_PRESENT)) continue;
        if (e & PAGE_HUGE) continue;
        uint64_t pdpt_phys = e & PAGE_ADDR_MASK;
        uint64_t* pdpt = (uint64_t*)phys_to_virt(pdpt_phys);

        for (int j = 0; j < 512; j++) {
            uint64_t e2 = pdpt[j];
            if (!(e2 & PAGE_PRESENT)) continue;
            if (e2 & PAGE_HUGE) continue;
            uint64_t pd_phys = e2 & PAGE_ADDR_MASK;
            uint64_t* pd = (uint64_t*)phys_to_virt(pd_phys);

            for (int k = 0; k < 512; k++) {
                uint64_t e3 = pd[k];
                if (!(e3 & PAGE_PRESENT)) continue;
                if (e3 & PAGE_HUGE) continue;
                uint64_t pt_phys = e3 & PAGE_ADDR_MASK;
                pmm_free_page(pt_phys);
            }
            pmm_free_page(pd_phys);
        }
        pmm_free_page(pdpt_phys);
    }
}

void vmm_destroy(AddressSpace* as) {
    if (!as || as == &g_kernel_as) return;

    for (VMA* v = as->vmas.head; v; v = v->next) {
        if (v->type == VMA_FRAMEBUFFER) continue;
        for (uint64_t addr = v->start; addr < v->end; addr += PAGE_SIZE) {
            uint64_t phys = paging_get_phys(as->pml4_phys, addr);
            if (phys) pmm_unref_page(phys);
        }
    }

    free_user_page_tables(as->pml4_phys);

    if (as->kstack_phys) {
        pmm_free_pages(as->kstack_phys, KERNEL_STACK_SIZE / PAGE_SIZE);
        as->kstack_phys = 0;
        as->kstack_top = 0;
    }

    vma_list_destroy(&as->vmas);

    pmm_free_page(as->pml4_phys);

    kfree(as);
}

int vmm_map(AddressSpace* as, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!as) return -1;
    if (phys == 0) {
        phys = pmm_alloc_page();
        if (!phys) return -1;
    }
    return paging_map_4k(as->pml4_phys, phys, virt, flags);
}

int vmm_map_alloc(AddressSpace* as, uint64_t virt, uint64_t flags) {
    return vmm_map(as, virt, 0, flags);
}

int vmm_unmap(AddressSpace* as, uint64_t virt) {
    if (!as) return -1;
    return paging_unmap_4k(as->pml4_phys, virt);
}

AddressSpace* vmm_clone(AddressSpace* src) {
    if (!src) return NULL;

    AddressSpace* dst = vmm_create();
    if (!dst) return NULL;

    for (VMA* vma = src->vmas.head; vma; vma = vma->next) {
        #ifdef DEBUG
        serial_emergency_print("clone: vma ");
        serial_emergency_print_hex(vma->start);
        serial_emergency_print("-");
        serial_emergency_print_hex(vma->end);
        serial_emergency_print(" flags=");
        serial_emergency_print_hex(vma->flags);
        serial_emergency_print("\n");
        #endif
        uint64_t new_flags = vma->flags & ~VMA_FLAG_COW;
        if (vma_add(&dst->vmas, vma->start, vma->end, new_flags, vma->type) != 0) {
            vmm_destroy(dst);
            return NULL;
        }

        for (uint64_t addr = vma->start; addr < vma->end; addr += PAGE_SIZE) {
            uint64_t phys = paging_get_phys(src->pml4_phys, addr);
            if (!phys) continue;

            uint64_t child_phys = phys;
            if (vma->type != VMA_FRAMEBUFFER && (new_flags & PAGE_WRITE)) {
                child_phys = pmm_alloc_page();
                if (!child_phys) {
                    vmm_destroy(dst);
                    return NULL;
                }
                uint8_t* source = (uint8_t*)phys_to_virt(phys);
                uint8_t* destination = (uint8_t*)phys_to_virt(child_phys);
                for (uint64_t offset = 0; offset < PAGE_SIZE; offset++) {
                    destination[offset] = source[offset];
                }
            } else if (vma->type != VMA_FRAMEBUFFER) {
                pmm_ref_page(phys);
            }
            if (paging_map_4k(dst->pml4_phys, child_phys, addr,
                              new_flags | PAGE_PRESENT | PAGE_USER) != 0) {
                if (child_phys != phys) pmm_free_page(child_phys);
                else if (vma->type != VMA_FRAMEBUFFER) pmm_unref_page(phys);
                vmm_destroy(dst);
                return NULL;
            }
        }
    }

    return dst;
}

int vmm_replace(AddressSpace* as) {
    if (!as) return -1;

    for (VMA* vma = as->vmas.head; vma; vma = vma->next) {
        for (uint64_t addr = vma->start; addr < vma->end; addr += PAGE_SIZE) {
            uint64_t phys = paging_get_phys(as->pml4_phys, addr);
            if (phys && vma->type != VMA_FRAMEBUFFER) {
                pmm_free_page(phys);
            }
            paging_unmap_4k(as->pml4_phys, addr);
        }
    }

    vma_list_destroy(&as->vmas);
    vma_list_init(&as->vmas);

    as->brk_start = USER_HEAP_BASE;
    as->brk_end = USER_HEAP_BASE;

    if (vmm_alloc_user_stack(as) != 0) return -1;

    return 0;
}

uint64_t vmm_get_phys(AddressSpace* as, uint64_t virt) {
    if (!as) return 0;
    return paging_get_phys(as->pml4_phys, virt);
}

int vmm_map_range_alloc(AddressSpace* as, uint64_t virt, uint64_t size,
                        uint64_t flags, VMAType type) {
    if (!as || size == 0) return -1;
    if ((virt & (PAGE_SIZE - 1)) != 0) return -1;

    uint64_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    if (vma_find_overlap(&as->vmas, virt, virt + pages * PAGE_SIZE)) {
        print("vmm_map_range_alloc: overlap\n");
        return -1;
    }

    for (uint64_t i = 0; i < pages; i++) {
        if (vmm_map_alloc(as, virt + i * PAGE_SIZE, flags) != 0) {
            for (uint64_t j = 0; j < i; j++) vmm_unmap(as, virt + j * PAGE_SIZE);
            return -1;
        }
    }

    if (vma_add(&as->vmas, virt, virt + pages * PAGE_SIZE, flags, type) != 0) {
        for (uint64_t j = 0; j < pages; j++) vmm_unmap(as, virt + j * PAGE_SIZE);
        return -1;
    }

    return 0;
}

int vmm_unmap_range(AddressSpace* as, uint64_t start, uint64_t end) {
    if (!as || start >= end) return -1;

    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
        vmm_unmap(as, addr);
    }
    vma_remove(&as->vmas, start, end);
    return 0;
}


void vmm_switch(AddressSpace* as) {
    if (!as) return;
    paging_switch(as->pml4_phys);
    g_current = as;

    if (as->kstack_top) {
        tss_set_rsp0(as->kstack_top);
        percpu_get(0)->kstack_top = as->kstack_top;
    }
}

int vmm_map_user_abi(AddressSpace* as) {
    if (!as) return -1;

    uint64_t phys = user_abi_phys();
    if (!phys) {
        print("vmm_map_user_abi: no ABI page\n");
        return -1;
    }

    uint64_t flags = PAGE_PRESENT | PAGE_USER;
    pmm_ref_page(phys);
    if (paging_map_4k(as->pml4_phys, phys, USER_ABI_VADDR, flags) != 0) {
        pmm_unref_page(phys);
        return -1;
    }

    if (vma_add(&as->vmas, USER_ABI_VADDR, USER_ABI_VADDR + PAGE_SIZE,
                flags, VMA_ABI) != 0) {
        paging_unmap_4k(as->pml4_phys, USER_ABI_VADDR);
        pmm_unref_page(phys);
        return -1;
    }

    return 0;
}

int vmm_alloc_user_stack(AddressSpace* as) {
    if (!as) return -1;

    uint64_t flags = PAGE_PRESENT | PAGE_WRITE | PAGE_USER;

    uint64_t pages = USER_STACK_SIZE / PAGE_SIZE;
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t virt = USER_STACK_LIMIT + i * PAGE_SIZE;
        if (vmm_map_alloc(as, virt, flags) != 0) {
            for (uint64_t j = 0; j < i; j++) {
                vmm_unmap(as, USER_STACK_LIMIT + j * PAGE_SIZE);
            }
            return -1;
        }
    }

    if (vma_add(&as->vmas, USER_STACK_LIMIT, USER_STACK_TOP, flags, VMA_STACK) != 0) {
        for (uint64_t j = 0; j < pages; j++) {
            vmm_unmap(as, USER_STACK_LIMIT + j * PAGE_SIZE);
        }
        return -1;
    }

    return 0;
}

int vmm_alloc_kernel_stack(AddressSpace* as) {
    if (!as) return -1;

    uint64_t pages = KERNEL_STACK_SIZE / PAGE_SIZE;
    uint64_t phys = pmm_alloc_pages((uint32_t)pages);
    if (!phys) {
        print("vmm_alloc_kernel_stack: no phys\n");
        return -1;
    }

    as->kstack_phys = phys;
    as->kstack_top = (uint64_t)(uintptr_t)phys_to_virt(phys + KERNEL_STACK_SIZE);
    return 0;
}

void vmm_free_kernel_stack(AddressSpace* as) {
    if (as->kstack_phys) {
        pmm_free_pages(as->kstack_phys, KERNEL_STACK_SIZE / PAGE_SIZE);
        as->kstack_phys = 0;
        as->kstack_top = 0;
    }
}

int vmm_enter_user(AddressSpace* as, uint64_t rip, uint64_t rsp) {
    if (!as) return -1;

    paging_switch(as->pml4_phys);
    tss_set_rsp0(as->kstack_top);
    percpu_get(0)->kstack_top = as->kstack_top;

    as->user_active = 1;

    int ret = vmm_enter_user_asm(as, rip, rsp);
    as->user_active = 0;
    return ret;
}

void vmm_save_user_context(AddressSpace* as, SyscallContext* ctx) {
    if (!as) return;
    as->user_ctx.rip    = ctx->rip;
    as->user_ctx.rsp    = ctx->rsp;
    as->user_ctx.rflags = ctx->rflags;
}