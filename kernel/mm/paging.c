#include "paging.h"
#include "pmm.h"
#include <serial/serial.h>
#include <stddef.h>

static uint64_t g_kernel_pml4_phys = 0;
static uint64_t g_max_phys         = 0;
static int      g_direct_map_ready = 0;

void paging_switch(uint64_t pml4_phys) {
    if (!pml4_phys) return;

    if (g_kernel_pml4_phys && pml4_phys != g_kernel_pml4_phys) {
        uint64_t* target = (uint64_t*)phys_to_virt(pml4_phys);
        uint64_t* kernel = (uint64_t*)phys_to_virt(g_kernel_pml4_phys);
        for (uint32_t i = 256; i < PML4_ENTRIES; i++) {
            target[i] = kernel[i];
        }
    }

    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

static inline uint64_t idx_pml4(uint64_t v) { return (v >> 39) & 0x1FF; }
static inline uint64_t idx_pdpt(uint64_t v) { return (v >> 30) & 0x1FF; }
static inline uint64_t idx_pd(uint64_t v)   { return (v >> 21) & 0x1FF; }
static inline uint64_t idx_pt(uint64_t v)   { return (v >> 12) & 0x1FF; }

static inline pte_t* table_at(uint64_t phys) {
    if (g_direct_map_ready) {
        return (pte_t*)phys_to_virt(phys);
    }
    return (pte_t*)(uintptr_t)phys;
}

static pte_t* get_or_create_table(pte_t* parent, uint64_t index,
                                  uint64_t flags_for_new) {
    uint64_t entry = parent[index];

    if (entry & PAGE_PRESENT) {
        if (entry & PAGE_HUGE) return NULL;
        return table_at(entry & PAGE_ADDR_MASK);
    }

    uint64_t phys = pmm_alloc_page();
    if (!phys) return NULL;

    uint8_t* p = (uint8_t*)table_at(phys);
    for (uint64_t i = 0; i < PAGE_SIZE; i++) p[i] = 0;

    parent[index] = phys | flags_for_new;
    return (pte_t*)p;
}

int paging_init(uint64_t kernel_pml4_phys,
                uint64_t kernel_phys_base,
                uint64_t kernel_size,
                uint64_t max_phys_addr) {
    if (!kernel_pml4_phys) {
        serial_emergency_print("paging: NULL PML4 phys\n");
        return -1;
    }

        (void)kernel_phys_base;
        (void)kernel_size;
    g_kernel_pml4_phys = kernel_pml4_phys;
    g_max_phys = max_phys_addr;
    g_direct_map_ready = 0;

    serial_emergency_print("paging: init pml4=");
    serial_emergency_print_hex(kernel_pml4_phys);
    serial_emergency_print(" max_phys=");
    serial_emergency_print_hex(max_phys_addr);
    serial_emergency_print("\n");

    uint64_t mapped = 0;
    uint64_t pml4_idx = idx_pml4(PHYS_MAP_BASE);

    pte_t* pml4 = table_at(kernel_pml4_phys);

    if (pml4[pml4_idx] & PAGE_PRESENT) {
        serial_emergency_print("paging: PML4[256] busy!\n");
        return -1;
    }

    uint64_t pdpt_phys = pmm_alloc_page();
    if (!pdpt_phys) return -1;
    {
        uint8_t* p = (uint8_t*)table_at(pdpt_phys);
        for (uint64_t i = 0; i < PAGE_SIZE; i++) p[i] = 0;
    }
    pml4[pml4_idx] = pdpt_phys | PAGE_PRESENT | PAGE_WRITE;

    pte_t* pdpt = table_at(pdpt_phys);

    while (mapped < max_phys_addr) {
        uint64_t pdpt_idx = idx_pdpt(PHYS_MAP_BASE + mapped);
        uint64_t pd_idx   = idx_pd(PHYS_MAP_BASE + mapped);

        if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
            uint64_t pd_phys = pmm_alloc_page();
            if (!pd_phys) return -1;
            uint8_t* p = (uint8_t*)table_at(pd_phys);
            for (uint64_t i = 0; i < PAGE_SIZE; i++) p[i] = 0;
            pdpt[pdpt_idx] = pd_phys | PAGE_PRESENT | PAGE_WRITE;
        }

        pte_t* pd = table_at(pdpt[pdpt_idx] & PAGE_ADDR_MASK);

        pd[pd_idx] = mapped | PAGE_PRESENT | PAGE_WRITE | PAGE_HUGE;
        mapped += PAGE_SIZE_2M;
    }

    serial_emergency_print("paging: direct map [");
    serial_emergency_print_hex(PHYS_MAP_BASE);
    serial_emergency_print("..");
    serial_emergency_print_hex(PHYS_MAP_BASE + mapped);
    serial_emergency_print(") -> phys [0..");
    serial_emergency_print_hex(mapped);
    serial_emergency_print(")\n");

    uint64_t cr3 = paging_get_cr3();
    if (cr3 != kernel_pml4_phys) {
        serial_emergency_print("paging: WARN cr3 != kernel_pml4\n");
        paging_switch(kernel_pml4_phys);
    }

    g_direct_map_ready = 1;

    __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");

    return 0;
}

int paging_map_4k(uint64_t pml4_phys, uint64_t phys, uint64_t virt,
                  uint64_t flags) {
    if (pml4_phys == 0) return -1;

    pte_t* pml4 = table_at(pml4_phys);
    uint64_t i4 = idx_pml4(virt);
    uint64_t i3 = idx_pdpt(virt);
    uint64_t i2 = idx_pd(virt);
    uint64_t i1 = idx_pt(virt);

    pte_t* pdpt = get_or_create_table(pml4, i4, PAGE_PRESENT | PAGE_WRITE);
    if (!pdpt) return -1;
    pte_t* pd = get_or_create_table(pdpt, i3, PAGE_PRESENT | PAGE_WRITE);
    if (!pd) return -1;
    pte_t* pt = get_or_create_table(pd, i2, PAGE_PRESENT | PAGE_WRITE);
    if (!pt) return -1;

    if (flags & PAGE_USER) {
        pml4[i4] |= PAGE_USER;
        pdpt[i3] |= PAGE_USER;
        pd[i2]  |= PAGE_USER;
    }

    pt[i1] = (phys & PAGE_ADDR_MASK) | flags | PAGE_PRESENT;

    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

int paging_unmap_4k(uint64_t pml4_phys, uint64_t virt) {
    if (pml4_phys == 0) return -1;

    pte_t* pml4 = table_at(pml4_phys);
    uint64_t e;

    e = pml4[idx_pml4(virt)];
    if (!(e & PAGE_PRESENT)) return -1;
    pte_t* pdpt = table_at(e & PAGE_ADDR_MASK);

    e = pdpt[idx_pdpt(virt)];
    if (!(e & PAGE_PRESENT)) return -1;
    if (e & PAGE_HUGE) return -1;
    pte_t* pd = table_at(e & PAGE_ADDR_MASK);

    e = pd[idx_pd(virt)];
    if (!(e & PAGE_PRESENT)) return -1;
    if (e & PAGE_HUGE) return -1;
    pte_t* pt = table_at(e & PAGE_ADDR_MASK);

    pt[idx_pt(virt)] = 0;

    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

uint64_t paging_get_phys(uint64_t pml4_phys, uint64_t virt) {
    if (pml4_phys == 0) return 0;

    pte_t* pml4 = table_at(pml4_phys);
    uint64_t e;

    e = pml4[idx_pml4(virt)];
    if (!(e & PAGE_PRESENT)) return 0;
    pte_t* pdpt = table_at(e & PAGE_ADDR_MASK);

    e = pdpt[idx_pdpt(virt)];
    if (!(e & PAGE_PRESENT)) return 0;
    if (e & PAGE_HUGE) {
        return 0;
    }
    pte_t* pd = table_at(e & PAGE_ADDR_MASK);

    e = pd[idx_pd(virt)];
    if (!(e & PAGE_PRESENT)) return 0;
    if (e & PAGE_HUGE) {
        uint64_t base = e & PAGE_ADDR_MASK & ~(PAGE_SIZE_2M - 1);
        return base;
    }
    pte_t* pt = table_at(e & PAGE_ADDR_MASK);

    e = pt[idx_pt(virt)];
    if (!(e & PAGE_PRESENT)) return 0;
    return e & PAGE_ADDR_MASK;
}

int paging_user_access(uint64_t pml4_phys, uint64_t virt, int write) {
    if (!pml4_phys || virt >= 0x0000800000000000ULL) return 0;

    pte_t* pml4 = table_at(pml4_phys);
    uint64_t e = pml4[idx_pml4(virt)];
    if (!(e & PAGE_PRESENT) || !(e & PAGE_USER) || (write && !(e & PAGE_WRITE))) return 0;
    pte_t* pdpt = table_at(e & PAGE_ADDR_MASK);

    e = pdpt[idx_pdpt(virt)];
    if (!(e & PAGE_PRESENT) || !(e & PAGE_USER) || (write && !(e & PAGE_WRITE))) return 0;
    if (e & PAGE_HUGE) return 1;
    pte_t* pd = table_at(e & PAGE_ADDR_MASK);

    e = pd[idx_pd(virt)];
    if (!(e & PAGE_PRESENT) || !(e & PAGE_USER) || (write && !(e & PAGE_WRITE))) return 0;
    if (e & PAGE_HUGE) return 1;
    pte_t* pt = table_at(e & PAGE_ADDR_MASK);

    e = pt[idx_pt(virt)];
    return (e & PAGE_PRESENT) && (e & PAGE_USER) && (!write || (e & PAGE_WRITE));
}

int paging_map_2m(uint64_t pml4_phys, uint64_t phys, uint64_t virt,
                  uint64_t flags) {
    if (pml4_phys == 0) return -1;
    if ((phys & (PAGE_SIZE_2M - 1)) || (virt & (PAGE_SIZE_2M - 1))) {
        return -1;
    }

    pte_t* pml4 = table_at(pml4_phys);

    pte_t* pdpt = get_or_create_table(pml4, idx_pml4(virt),
                                      PAGE_PRESENT | PAGE_WRITE);
    if (!pdpt) return -1;

    pte_t* pd = get_or_create_table(pdpt, idx_pdpt(virt),
                                    PAGE_PRESENT | PAGE_WRITE);
    if (!pd) return -1;

    pd[idx_pd(virt)] = (phys & PAGE_ADDR_MASK) | flags | PAGE_PRESENT | PAGE_HUGE;
    return 0;
}

uint64_t paging_kernel_pml4_phys(void) { return g_kernel_pml4_phys; }
uint64_t paging_max_phys(void)         { return g_max_phys; }