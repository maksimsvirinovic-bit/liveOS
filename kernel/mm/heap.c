#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include <serial/serial.h>
#include <stddef.h>

#define HEAP_CHUNK_SIZE  (8 * 1024 * 1024)
#define HEAP_MAGIC       0xC0FFEE42
#define MAX_CHUNKS       128

typedef struct Block {
    uint32_t magic;
    uint32_t free;
    size_t   size;
    struct Block* next;
} Block;

#define BLOCK_HDR_SIZE   (sizeof(Block))
#define ALIGN16(x)       (((x) + 15) & ~((size_t)15))

typedef struct HeapChunk {
    uint64_t virt;
    uint64_t size;
} HeapChunk;

struct Heap {
    uint64_t virt_base;
    uint64_t virt_end;
    uint64_t virt_limit;

    Block* first;
    size_t used;

    HeapChunk chunks[MAX_CHUNKS];
    uint32_t  chunk_count;

    HeapGrowFn grow;
    void*      grow_ctx;

    uint64_t   pml4_phys;
    int        is_user_heap;
};


static int heap_grow(Heap* h) {
    if (h->virt_end + HEAP_CHUNK_SIZE > h->virt_limit) {
        serial_emergency_print("heap: grow beyond limit\n");
        return -1;
    }
    if (h->chunk_count >= MAX_CHUNKS) {
        serial_emergency_print("heap: max chunks reached\n");
        return -1;
    }

    uint64_t chunk_virt = h->virt_end;
    uint64_t chunk_size = HEAP_CHUNK_SIZE;

    if (h->grow(h, h->grow_ctx, chunk_virt, chunk_size) != 0) {
        serial_emergency_print("heap: grow callback failed\n");
        return -1;
    }

    Block* b = (Block*)(uintptr_t)chunk_virt;
    b->magic = HEAP_MAGIC;
    b->free  = 1;
    b->size  = chunk_size - BLOCK_HDR_SIZE;
    b->next  = NULL;

    if (!h->first) {
        h->first = b;
    } else {
        Block* cur = h->first;
        while (cur->next) cur = cur->next;
        cur->next = b;
    }

    h->chunks[h->chunk_count].virt = chunk_virt;
    h->chunks[h->chunk_count].size = chunk_size;
    h->chunk_count++;

    h->virt_end += chunk_size;
    return 0;
}


Heap* heap_create(uint64_t virt_base, uint64_t max_size,
                  HeapGrowFn grow, void* grow_ctx) {
    if (max_size < HEAP_CHUNK_SIZE) return NULL;
    if (virt_base + max_size < virt_base) return NULL;

    Heap* h = (Heap*)kmalloc(sizeof(Heap));
    if (!h) return NULL;

    h->virt_base = virt_base;
    h->virt_end  = virt_base;
    h->virt_limit = virt_base + max_size;
    h->first = NULL;
    h->used = 0;
    h->chunk_count = 0;
    h->grow = grow;
    h->grow_ctx = grow_ctx;
    h->pml4_phys = paging_kernel_pml4_phys();
    h->is_user_heap = 0;

    if (heap_grow(h) != 0) {
        kfree(h);
        return NULL;
    }
    return h;
}

void heap_destroy(Heap* h) {
    if (!h) return;

    for (uint32_t i = 0; i < h->chunk_count; i++) {
        uint64_t virt = h->chunks[i].virt;
        uint64_t size = h->chunks[i].size;
        uint64_t pages = size / PAGE_SIZE;

        for (uint64_t p = 0; p < pages; p++) {
            uint64_t v = virt + p * PAGE_SIZE;
            uint64_t phys = paging_get_phys(h->pml4_phys, v);
            if (phys) {
                paging_unmap_4k(h->pml4_phys, v);
                pmm_free_page(phys);
            }
        }
    }

    h->chunk_count = 0;
    h->first = NULL;
    h->used = 0;

    if (!h->is_user_heap || 1) {
    }
    if (h != kheap()) {
        kfree(h);
    }
}


static Block* find_free(Heap* h, size_t size) {
    for (Block* b = h->first; b; b = b->next) {
        if (b->free && b->size >= size) return b;
    }
    return NULL;
}

static void split_block(Block* b, size_t size) {
    if (b->size < size + BLOCK_HDR_SIZE + 16) return;
    Block* nb = (Block*)((uint8_t*)b + BLOCK_HDR_SIZE + size);
    nb->magic = HEAP_MAGIC;
    nb->free  = 1;
    nb->size  = b->size - size - BLOCK_HDR_SIZE;
    nb->next  = b->next;
    b->size = size;
    b->next = nb;
}

void* heap_alloc(Heap* h, size_t size) {
    if (!h || size == 0) return NULL;
    size = ALIGN16(size);

    for (int attempt = 0; attempt < 2; attempt++) {
        Block* b = find_free(h, size);
        if (b) {
            split_block(b, size);
            b->free = 0;
            h->used += BLOCK_HDR_SIZE + b->size;
            return (void*)((uint8_t*)b + BLOCK_HDR_SIZE);
        }
        if (heap_grow(h) != 0) return NULL;
    }
    return NULL;
}

void* heap_zalloc(Heap* h, size_t size) {
    void* p = heap_alloc(h, size);
    if (p) {
        uint8_t* b = (uint8_t*)p;
        for (size_t i = 0; i < size; i++) b[i] = 0;
    }
    return p;
}

void* heap_realloc(Heap* h, void* ptr, size_t new_size) {
    if (!ptr) return heap_alloc(h, new_size);
    if (new_size == 0) { heap_free(h, ptr); return NULL; }

    Block* b = (Block*)((uint8_t*)ptr - BLOCK_HDR_SIZE);
    if (b->magic != HEAP_MAGIC) return NULL;

    size_t old_size = b->size;
    if (old_size >= new_size) return ptr;

    void* np = heap_alloc(h, new_size);
    if (!np) return NULL;
    for (size_t i = 0; i < old_size; i++) ((uint8_t*)np)[i] = ((uint8_t*)ptr)[i];
    heap_free(h, ptr);
    return np;
}

void heap_free(Heap* h, void* ptr) {
    if (!h || !ptr) return;
    Block* b = (Block*)((uint8_t*)ptr - BLOCK_HDR_SIZE);
    if (b->magic != HEAP_MAGIC) {
        serial_emergency_print("heap_free: bad magic\n");
        return;
    }
    if (b->free) {
        serial_emergency_print("heap_free: double free\n");
        return;
    }
    b->free = 1;
    h->used -= BLOCK_HDR_SIZE + b->size;

    if (b->next && b->next->free) {
        Block* nb = b->next;
        b->size += BLOCK_HDR_SIZE + nb->size;
        b->next = nb->next;
    }
}

size_t heap_total_bytes(Heap* h) { return h ? h->virt_end - h->virt_base : 0; }
size_t heap_used_bytes(Heap* h)  { return h ? h->used : 0; }
size_t heap_free_bytes(Heap* h)  { return heap_total_bytes(h) - heap_used_bytes(h); }

void heap_dump(Heap* h) {
    if (!h) return;
    serial_emergency_print("heap dump (chunks=");
    serial_emergency_print_hex(h->chunk_count);
    serial_emergency_print("):\n");
    int i = 0;
    for (Block* b = h->first; b; b = b->next) {
        serial_emergency_print("  [");
        serial_emergency_print_hex(i++);
        serial_emergency_print("] @");
        serial_emergency_print_hex((uint64_t)(uintptr_t)b);
        serial_emergency_print(" size=");
        serial_emergency_print_hex(b->size);
        serial_emergency_print(b->free ? " FREE\n" : " used\n");
    }
}


static Heap* g_kheap = NULL;
static Heap  g_kheap_storage;

static int kernel_heap_grow(Heap* h, void* ctx, uint64_t virt, uint64_t size) {
    (void)h; (void)ctx;
    uint64_t pages = size / PAGE_SIZE;
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) {
            serial_emergency_print("kernel_heap_grow: no phys\n");
            return -1;
        }
        if (paging_map_4k(paging_kernel_pml4_phys(), phys, virt + i * PAGE_SIZE,
                          PAGE_PRESENT | PAGE_WRITE) != 0) {
            pmm_free_page(phys);
            return -1;
        }
    }
    return 0;
}

int kheap_init(void) {
    g_kheap_storage.virt_base  = KERNEL_HEAP_BASE;
    g_kheap_storage.virt_end   = KERNEL_HEAP_BASE;
    g_kheap_storage.virt_limit = KERNEL_HEAP_BASE + KERNEL_HEAP_SIZE;
    g_kheap_storage.first      = NULL;
    g_kheap_storage.used       = 0;
    g_kheap_storage.chunk_count = 0;
    g_kheap_storage.grow       = kernel_heap_grow;
    g_kheap_storage.grow_ctx   = NULL;
    g_kheap_storage.pml4_phys  = paging_kernel_pml4_phys();
    g_kheap_storage.is_user_heap = 0;

    g_kheap = &g_kheap_storage;

    if (heap_grow(g_kheap) != 0) {
        serial_emergency_print("kheap: first grow failed\n");
        g_kheap = NULL;
        return -1;
    }

    serial_emergency_print("kheap: init @");
    serial_emergency_print_hex(KERNEL_HEAP_BASE);
    serial_emergency_print(" size=");
    serial_emergency_print_hex((uint64_t)heap_total_bytes(g_kheap));
    serial_emergency_print("\n");
    return 0;
}

Heap* kheap(void) { return g_kheap; }

void* kmalloc(size_t size)        { return heap_alloc(g_kheap, size); }
void* kzalloc(size_t size)        { return heap_zalloc(g_kheap, size); }
void* krealloc(void* p, size_t n) { return heap_realloc(g_kheap, p, n); }
void  kfree(void* p)              { heap_free(g_kheap, p); }
size_t kmalloc_total(void)        { return heap_total_bytes(g_kheap); }
size_t kmalloc_used(void)         { return heap_used_bytes(g_kheap); }
size_t kmalloc_free(void)         { return heap_free_bytes(g_kheap); }