#ifndef PMM_H
#define PMM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <boot_info.h>

#define EFI_RESERVED              0
#define EFI_LOADER_CODE           1
#define EFI_LOADER_DATA           2
#define EFI_BOOT_SERVICES_CODE    3
#define EFI_BOOT_SERVICES_DATA    4
#define EFI_RUNTIME_SERVICES_CODE 5
#define EFI_RUNTIME_SERVICES_DATA 6
#define EFI_CONVENTIONAL_MEMORY   7
#define EFI_UNUSABLE_MEMORY       8
#define EFI_ACPI_RECLAIM_MEMORY   9
#define EFI_ACPI_MEMORY_NVS       10
#define EFI_MEMORY_MAPPED_IO      11
#define EFI_MEMORY_MAPPED_IO_PORT 12
#define EFI_PAL_CODE              13
#define EFI_PERSISTENT_MEMORY     14

typedef struct {
    uint32_t Type;
    uint32_t Pad;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} __attribute__((packed)) EFI_MEMORY_DESCRIPTOR;

#define PAGE_SIZE 4096ULL

void     pmm_init(const BOOT_INFO* info);
uint64_t pmm_alloc_page(void);
uint64_t pmm_alloc_pages(uint32_t count);
void     pmm_free_page(uint64_t page);
void     pmm_free_pages(uint64_t page_addr, uint32_t count);
void     pmm_ref_page(uint64_t page_addr);
void     pmm_unref_page(uint64_t page_addr);
uint32_t pmm_page_refcount(uint64_t page_addr);
uint64_t pmm_total_pages(void);
uint64_t pmm_available_pages(void);
uint64_t pmm_used_pages(void);

#ifdef __cplusplus
}
#endif

#endif