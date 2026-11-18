#include "pmm.h"
#include <serial/serial.h>
#include <stddef.h>
#include <str.h>

#define PMM_BITMAP_MAX_BYTES (1024 * 1024)

static uint8_t  g_bitmap[PMM_BITMAP_MAX_BYTES] __attribute__((aligned(4096)));
static uint8_t* bitmap       = NULL;
static uint32_t g_page_refs[65536];
static uint64_t bitmap_bytes = 0;
static uint64_t total_pages  = 0;
static uint64_t free_pages   = 0;
static uint64_t last_alloc_hint = 0;

extern char __image_base[];
extern char __kernel_end[];

static inline void bitmap_set(uint64_t page) {
    bitmap[page >> 3] |= (uint8_t)(1u << (page & 7));
}

static inline void bitmap_clear(uint64_t page) {
    bitmap[page >> 3] &= (uint8_t)~(1u << (page & 7));
}

static inline int bitmap_test(uint64_t page) {
    return (bitmap[page >> 3] >> (page & 7)) & 1;
}

static inline void mark_used(uint64_t page) {
    if (page >= total_pages) return;
    if (!bitmap_test(page)) {
        bitmap_set(page);
        free_pages--;
    }
}

static inline void mark_free(uint64_t page) {
    if (page >= total_pages) return;
    if (bitmap_test(page)) {
        bitmap_clear(page);
        free_pages++;
    }
}

static uint64_t compute_max_phys(const BOOT_INFO* info) {
    uint64_t max = 0;
    const uint8_t* p = (const uint8_t*)(uintptr_t)info->MemoryMapAddr;
    uint64_t count = info->MemoryMapSize / info->MemoryMapDescriptorSize;
    for (uint64_t i = 0; i < count; i++) {
        const EFI_MEMORY_DESCRIPTOR* d =
            (const EFI_MEMORY_DESCRIPTOR*)(p + i * info->MemoryMapDescriptorSize);

        uint32_t type = d->Type;
        if (type != EFI_CONVENTIONAL_MEMORY &&
            type != EFI_BOOT_SERVICES_CODE &&
            type != EFI_BOOT_SERVICES_DATA &&
            type != EFI_LOADER_CODE &&
            type != EFI_LOADER_DATA &&
            type != EFI_RUNTIME_SERVICES_CODE &&
            type != EFI_RUNTIME_SERVICES_DATA &&
            type != EFI_ACPI_RECLAIM_MEMORY &&
            type != EFI_ACPI_MEMORY_NVS) continue;

        uint64_t end = d->PhysicalStart + d->NumberOfPages * PAGE_SIZE;
        if (end > max) max = end;
    }
    return max;
}

void pmm_init(const BOOT_INFO* info) {
    if (!info || !info->MemoryMapAddr || !info->MemoryMapSize) {
        serial_emergency_print("PMM: no memory map!\n");
        return;
    }

    uint64_t max_phys = compute_max_phys(info);
    if (max_phys == 0) {
        serial_emergency_print("PMM: no usable RAM!\n");
        return;
    }

    total_pages = max_phys / PAGE_SIZE;
    bitmap_bytes = (total_pages + 7) / 8;

    if (bitmap_bytes > PMM_BITMAP_MAX_BYTES) {
        serial_emergency_print("PMM: RAM too big, bitmap overflow!\n");
        serial_emergency_print("PMM: need ");
        serial_emergency_print_hex(bitmap_bytes);
        serial_emergency_print(" bytes, have ");
        serial_emergency_print_hex(PMM_BITMAP_MAX_BYTES);
        serial_emergency_print("\n");
        total_pages = 0;
        return;
    }

    bitmap = g_bitmap;
    memset(g_page_refs, 0, sizeof(g_page_refs));
    last_alloc_hint = 0;
    free_pages = 0;

    serial_emergency_print("PMM: bitmap size = ");
    serial_emergency_print_hex(bitmap_bytes);
    serial_emergency_print(", total pages = ");
    serial_emergency_print_hex(total_pages);
    serial_emergency_print("\n");

    for (uint64_t i = 0; i < bitmap_bytes; i++) bitmap[i] = 0xFF;

    const uint8_t* p = (const uint8_t*)(uintptr_t)info->MemoryMapAddr;
    uint64_t count = info->MemoryMapSize / info->MemoryMapDescriptorSize;

    for (uint64_t i = 0; i < count; i++) {
        const EFI_MEMORY_DESCRIPTOR* d =
            (const EFI_MEMORY_DESCRIPTOR*)(p + i * info->MemoryMapDescriptorSize);

        uint32_t type = d->Type;
        uint64_t start_page = d->PhysicalStart / PAGE_SIZE;
        uint64_t pages = d->NumberOfPages;

        if (type == EFI_CONVENTIONAL_MEMORY ||
            type == EFI_BOOT_SERVICES_CODE ||
            type == EFI_BOOT_SERVICES_DATA) {
            for (uint64_t j = 0; j < pages; j++) {
                mark_free(start_page + j);
            }
        }
    }

    for (uint64_t page = 0; page < 0x100; page++) {
        mark_used(page);
    }

    mark_used(0x5000 / PAGE_SIZE);

    if (info->KernelPhysBase && info->KernelPhysSize) {
        uint64_t k_start = info->KernelPhysBase / PAGE_SIZE;
        uint64_t k_end   = (info->KernelPhysBase + info->KernelPhysSize + PAGE_SIZE - 1) / PAGE_SIZE;
        for (uint64_t page = k_start; page < k_end; page++) {
            mark_used(page);
        }
        serial_emergency_print("PMM: kernel phys ");
        serial_emergency_print_hex(info->KernelPhysBase);
        serial_emergency_print("..");
        serial_emergency_print_hex(info->KernelPhysBase + info->KernelPhysSize);
        serial_emergency_print("\n");
    }

    uint64_t mmap_start = info->MemoryMapAddr / PAGE_SIZE;
    uint64_t mmap_end   = (info->MemoryMapAddr + info->MemoryMapSize + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t page = mmap_start; page < mmap_end; page++) {
        mark_used(page);
    }

    if (info->InitramfsAddr && info->InitramfsSize) {
        uint64_t i_start = info->InitramfsAddr / PAGE_SIZE;
        uint64_t i_end   = (info->InitramfsAddr + info->InitramfsSize + PAGE_SIZE - 1) / PAGE_SIZE;
        for (uint64_t page = i_start; page < i_end; page++) {
            mark_used(page);
        }
    }

    if (info->FbAddr) {
        uint64_t fb_size = (uint64_t)info->Pitch * info->Height;
        uint64_t fb_start = info->FbAddr / PAGE_SIZE;
        uint64_t fb_end   = (info->FbAddr + fb_size + PAGE_SIZE - 1) / PAGE_SIZE;
        for (uint64_t page = fb_start; page < fb_end; page++) {
            mark_used(page);
        }
    }

    serial_emergency_print("PMM: free pages = ");
    serial_emergency_print_hex(free_pages);
    serial_emergency_print(" (");
    serial_emergency_print_hex(free_pages * PAGE_SIZE / 1024 / 1024);
    serial_emergency_print(" MiB)\n");
}

uint64_t pmm_alloc_page(void) {
    if (!bitmap || total_pages == 0) return 0;
    if (free_pages == 0) {
        serial_emergency_print("pmm_alloc: no free pages\n");
        return 0;
    }

    for (uint64_t pass = 0; pass < 2; pass++) {
        uint64_t start = (pass == 0) ? last_alloc_hint : 0;
        uint64_t end   = (pass == 0) ? total_pages     : last_alloc_hint;

        for (uint64_t page = start; page < end; page++) {
            if (!bitmap_test(page)) {
                bitmap_set(page);
                free_pages--;
                last_alloc_hint = page + 1;
                g_page_refs[page] = 1;
                return page * PAGE_SIZE;
            }
        }
    }

    serial_emergency_print("pmm_alloc: bitmap scan failed, free=");
    serial_emergency_print_hex(free_pages);
    serial_emergency_print("\n");
    return 0;
}

void pmm_ref_page(uint64_t page_addr) {
    if (!bitmap) return;
    uint64_t page = page_addr / PAGE_SIZE;
    if (page >= total_pages) return;
    if (bitmap_test(page)) g_page_refs[page]++;
}

void pmm_unref_page(uint64_t page_addr) {
    if (!bitmap) return;
    uint64_t page = page_addr / PAGE_SIZE;
    if (page >= total_pages) return;
    if (g_page_refs[page] > 0) {
        g_page_refs[page]--;
        if (g_page_refs[page] == 0) {
            if (bitmap_test(page)) {
                bitmap_clear(page);
                free_pages++;
                if (page < last_alloc_hint) last_alloc_hint = page;
            }
        }
    }
}

uint32_t pmm_page_refcount(uint64_t page_addr) {
    if (!bitmap) return 0;
    uint64_t page = page_addr / PAGE_SIZE;
    if (page >= total_pages) return 0;
    return g_page_refs[page];
}

void pmm_free_page(uint64_t page_addr) {
    if (!bitmap) return;
    uint64_t page = page_addr / PAGE_SIZE;
    if (page >= total_pages) return;
    if (g_page_refs[page] > 0) {
        g_page_refs[page]--;
        if (g_page_refs[page] == 0) {
            if (bitmap_test(page)) {
                bitmap_clear(page);
                free_pages++;
                if (page < last_alloc_hint) last_alloc_hint = page;
            }
        }
    }
}

uint64_t pmm_alloc_pages(uint32_t count) {
    if (count == 0 || free_pages < count) return 0;

    for (uint64_t pass = 0; pass < 2; pass++) {
        uint64_t start = (pass == 0) ? last_alloc_hint : 0;
        uint64_t end   = (pass == 0) ? total_pages     : last_alloc_hint;

        uint64_t run_start = 0;
        uint32_t run_len = 0;

        for (uint64_t page = start; page < end; page++) {
            if (!bitmap_test(page)) {
                if (run_len == 0) run_start = page;
                run_len++;
                if (run_len == count) {
                    for (uint32_t i = 0; i < count; i++) {
                        bitmap_set(run_start + i);
                        free_pages--;
                        g_page_refs[run_start + i] = 1;
                    }
                    last_alloc_hint = run_start + count;
                    return run_start * PAGE_SIZE;
                }
            } else {
                run_len = 0;
            }
        }
    }
    return 0;
}

void pmm_free_pages(uint64_t page_addr, uint32_t count) {
    uint64_t page = page_addr / PAGE_SIZE;
    for (uint32_t i = 0; i < count; i++) {
        if (page + i < total_pages) {
            uint64_t idx = page + i;
            if (g_page_refs[idx] > 0) {
                g_page_refs[idx]--;
                if (g_page_refs[idx] == 0) {
                    g_page_refs[idx] = 0;
                    if (bitmap_test(idx)) {
                        bitmap_clear(idx);
                        free_pages++;
                    }
                }
            }
        }
    }
    if (page < last_alloc_hint) last_alloc_hint = page;
}

uint64_t pmm_total_pages(void) { return total_pages; }
uint64_t pmm_available_pages(void)  { return free_pages; }
uint64_t pmm_used_pages(void)  { return total_pages - free_pages; }