#include "sys.h"
#include <stdint.h>
#include <stddef.h>

#define MALLOC_ARENA_SIZE (16u * 1024u * 1024u)
#define MALLOC_ALIGN 16u
#define MALLOC_MAGIC 0x4F4C6D53  //   "LOSm"

typedef struct BlockHeader {
    uint32_t magic;
    uint32_t size;
    struct BlockHeader* next;
} BlockHeader;

static uint8_t* g_arena = NULL;
static size_t   g_arena_size = 0;
static size_t   g_arena_used = 0;
static BlockHeader* g_free_list = NULL;

static int ensure_arena(void) {
    if (g_arena) return 0;
    void* p = (void*)sys_mmap_anon(MALLOC_ARENA_SIZE);
    if ((intptr_t)p < 0) return -1;
    g_arena = (uint8_t*)p;
    g_arena_size = MALLOC_ARENA_SIZE;
    g_arena_used = 0;
    g_free_list = NULL;
    return 0;
}

void* malloc(size_t size) {
    if (size == 0) return NULL;
    size = (size + MALLOC_ALIGN - 1) & ~(MALLOC_ALIGN - 1);
    if (ensure_arena() != 0) return NULL;

    BlockHeader** pp = &g_free_list;
    while (*pp) {
        BlockHeader* b = *pp;
        if (b->size >= size) {
            *pp = b->next;
            b->magic = MALLOC_MAGIC;
            return (void*)(b + 1);
        }
        pp = &b->next;
    }

    size_t need = sizeof(BlockHeader) + size;
    if (g_arena_used + need > g_arena_size) return NULL;
    BlockHeader* b = (BlockHeader*)(g_arena + g_arena_used);
    b->magic = MALLOC_MAGIC;
    b->size = (uint32_t)size;
    b->next = NULL;
    g_arena_used += need;
    return (void*)(b + 1);
}

void free(void* ptr) {
    if (!ptr) return;
    BlockHeader* b = ((BlockHeader*)ptr) - 1;
    if (b->magic != MALLOC_MAGIC) return;
    b->magic = 0;
    b->next = g_free_list;
    g_free_list = b;
}

void* calloc(size_t n, size_t size) {
    size_t total = n * size;
    if (n && total / n != size) return NULL;
    void* p = malloc(total);
    if (p) {
        uint8_t* b = (uint8_t*)p;
        for (size_t i = 0; i < total; i++) b[i] = 0;
    }
    return p;
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }
    BlockHeader* b = ((BlockHeader*)ptr) - 1;
    if (b->magic != MALLOC_MAGIC) return NULL;
    if (b->size >= size) return ptr;
    void* np = malloc(size);
    if (!np) return NULL;
    uint8_t* src = (uint8_t*)ptr;
    uint8_t* dst = (uint8_t*)np;
    for (size_t i = 0; i < b->size; i++) dst[i] = src[i];
    free(ptr);
    return np;
}