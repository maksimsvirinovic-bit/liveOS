#ifndef VMALLOC_H
#define VMALLOC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define VMALLOC_BASE  0xFFFFC00000000000ULL
#define VMALLOC_SIZE  (512ULL * 1024 * 1024 * 1024)
#define VMALLOC_END   (VMALLOC_BASE + VMALLOC_SIZE)

#define KERNEL_STACK_SIZE  (128ULL * 1024)

void vmalloc_init(void);

uint64_t vmalloc_reserve(uint64_t size);

void* vmalloc_page(void);

uint64_t vmalloc_kernel_stack(void);

void vfree(void* ptr);

void vfree_kernel_stack(uint64_t base);

uint64_t vmalloc_used(void);
uint64_t vmalloc_total(void);
void     vmalloc_dump(void);

#ifdef __cplusplus
}
#endif

#endif