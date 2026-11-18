#ifndef HEAP_H
#define HEAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

typedef struct Heap Heap;

typedef int (*HeapGrowFn)(Heap* h, void* ctx, uint64_t virt, uint64_t size);

Heap* heap_create(uint64_t virt_base, uint64_t max_size,
                  HeapGrowFn grow, void* grow_ctx);
void  heap_destroy(Heap* h);

void* heap_alloc(Heap* h, size_t size);
void* heap_zalloc(Heap* h, size_t size);
void* heap_realloc(Heap* h, void* ptr, size_t new_size);
void  heap_free(Heap* h, void* ptr);

size_t heap_total_bytes(Heap* h);
size_t heap_used_bytes(Heap* h);
size_t heap_free_bytes(Heap* h);
void   heap_dump(Heap* h);

int   kheap_init(void);
Heap* kheap(void);

void* kmalloc(size_t size);
void* kzalloc(size_t size);
void* krealloc(void* ptr, size_t new_size);
void  kfree(void* ptr);

size_t kmalloc_total(void);
size_t kmalloc_used(void);
size_t kmalloc_free(void);

#ifdef __cplusplus
}
#endif

#endif