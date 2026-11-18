#ifndef PAGING_H
#define PAGING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define PAGE_SIZE    4096ULL
#define PAGE_SIZE_2M (2ULL * 1024 * 1024)

#define PML4_ENTRIES 512
#define PDPT_ENTRIES 512
#define PD_ENTRIES   512
#define PT_ENTRIES   512

#define PAGE_PRESENT (1ULL << 0)
#define PAGE_WRITE   (1ULL << 1)
#define PAGE_USER    (1ULL << 2)
#define PAGE_PWT     (1ULL << 3)
#define PAGE_PCD     (1ULL << 4)
#define PAGE_ACCESS  (1ULL << 5)
#define PAGE_DIRTY   (1ULL << 6)
#define PAGE_HUGE    (1ULL << 7)
#define PAGE_GLOBAL  (1ULL << 8)
#define PAGE_NX      (1ULL << 63)

#define PAGE_NOCACHE (PAGE_PWT | PAGE_PCD)

#define PAGE_ADDR_MASK 0x000FFFFFFFFFF000ULL

#define PHYS_MAP_BASE         0xFFFF800000000000ULL
#define KERNEL_VMALLOC_BASE   0xFFFFC00000000000ULL
#define KERNEL_VMALLOC_SIZE   (512ULL * 1024 * 1024 * 1024)
#define KERNEL_HEAP_BASE      0xFFFFE00000000000ULL
#define KERNEL_HEAP_SIZE      (512ULL * 1024 * 1024 * 1024)
#define KERNEL_VIRT_BASE      0xFFFFFFFF80000000ULL

#define PTE_VALID_FLAGS (PAGE_PRESENT | PAGE_WRITE | PAGE_USER | PAGE_PWT | PAGE_PCD | PAGE_ACCESS | PAGE_DIRTY | PAGE_HUGE | PAGE_GLOBAL | PAGE_NX)
   
typedef uint64_t pte_t;
typedef pte_t*   ptbl_t;

int paging_init(uint64_t kernel_pml4_phys,
                uint64_t kernel_phys_base,
                uint64_t kernel_size,
                uint64_t max_phys_addr);

int paging_map_4k(uint64_t pml4_phys, uint64_t phys, uint64_t virt,
                  uint64_t flags);

int paging_unmap_4k(uint64_t pml4_phys, uint64_t virt);

uint64_t paging_get_phys(uint64_t pml4_phys, uint64_t virt);
int paging_user_access(uint64_t pml4_phys, uint64_t virt, int write);

int paging_map_2m(uint64_t pml4_phys, uint64_t phys, uint64_t virt,
                  uint64_t flags);

void paging_switch(uint64_t pml4_phys);

static inline uint64_t paging_get_cr3(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & PAGE_ADDR_MASK;
}

#define phys_to_virt(p) ((void*)((uint64_t)(p) + PHYS_MAP_BASE))
#define virt_to_phys(v) ((uint64_t)(v) - PHYS_MAP_BASE)

static inline int is_phys_map_addr(uint64_t v) {
    return v >= PHYS_MAP_BASE && v < PHYS_MAP_BASE + (1ULL << 46);
}

uint64_t paging_kernel_pml4_phys(void);
uint64_t paging_max_phys(void);

#ifdef __cplusplus
}
#endif

#endif