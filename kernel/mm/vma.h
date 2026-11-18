#ifndef VMA_H
#define VMA_H

#include <stdint.h>

typedef enum {
    VMA_ANON    = 0,
    VMA_FILE    = 1,
    VMA_STACK   = 2,
    VMA_ABI     = 3,
    VMA_FRAMEBUFFER = 4,
    VMA_BACKBUFFER = 5,
} VMAType;

typedef struct VMA {
    uint64_t start;
    uint64_t end;
    uint64_t flags;
    VMAType  type;
    struct VMA* next;

    void*    file_data;
    uint64_t file_offset;
    uint64_t file_size;
} VMA;

typedef struct {
    VMA*     head;
    uint64_t count;
} VMAList;

#define VMA_FLAG_COW  (1ULL << 9)

void   vma_list_init(VMAList* list);
void   vma_list_destroy(VMAList* list);

VMA*   vma_find(VMAList* list, uint64_t addr);
VMA*   vma_find_overlap(VMAList* list, uint64_t start, uint64_t end);

uint64_t vma_find_free(VMAList* list, uint64_t lo, uint64_t hi, uint64_t size);

int    vma_add(VMAList* list, uint64_t start, uint64_t end,
               uint64_t flags, VMAType type);

int    vma_remove(VMAList* list, uint64_t start, uint64_t end);

void   vma_dump(VMAList* list);

#endif