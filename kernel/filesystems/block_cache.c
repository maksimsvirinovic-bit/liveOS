#include "block_cache.h"
#include "serial/serial.h"
#include <str.h>
#include <stddef.h>

typedef struct {
    BlockDevice* dev;
    uint64_t     lba;
    int          dirty;
    int          pins;
    uint8_t      data[BCACHE_BLOCK_SIZE];
} BCacheEntry;

static BCacheEntry g_cache[BCACHE_ENTRIES];
static uint64_t    g_hits, g_misses, g_evictions, g_writes;

static inline int hash_slot(uint64_t lba) {
    return (int)((lba * 2654435761ULL) % BCACHE_ENTRIES);
}

static inline uint64_t bcache_irq_save(void) {
    uint64_t f; __asm__ volatile("pushfq; popq %0; cli" : "=r"(f) :: "memory");
    return f;
}
static inline void bcache_irq_restore(uint64_t f) {
    if (f & (1u << 9)) __asm__ volatile("sti" ::: "memory");
}

void bcache_init(void) {
    for (int i = 0; i < BCACHE_ENTRIES; i++) {
        g_cache[i].dev = NULL;
        g_cache[i].lba = 0;
        g_cache[i].dirty = 0;
        g_cache[i].pins = 0;
    }
    g_hits = g_misses = g_evictions = g_writes = 0;
    serial_emergency_print("bcache: init, entries=");
    serial_emergency_print_hex(BCACHE_ENTRIES);
    serial_emergency_print(" (");
    serial_emergency_print_hex((uint64_t)BCACHE_ENTRIES * BCACHE_BLOCK_SIZE / 1024);
    serial_emergency_print(" KiB)\n");
}

static BCacheEntry* find_entry(BlockDevice* dev, uint64_t lba) {
    int slot = hash_slot(lba);
    for (int i = 0; i < 8; i++) {
        BCacheEntry* e = &g_cache[(slot + i) % BCACHE_ENTRIES];
        if (e->dev == dev && e->lba == lba) return e;
        if (e->dev == NULL) return NULL;
    }
    return NULL;
}

static BCacheEntry* alloc_entry(BlockDevice* dev, uint64_t lba) {
    int slot = hash_slot(lba);
    BCacheEntry* victim = NULL;

    for (int i = 0; i < 8; i++) {
        BCacheEntry* e = &g_cache[(slot + i) % BCACHE_ENTRIES];
        if (e->dev == NULL) return e;
        if (e->dev == dev && e->lba == lba) return e;
        if (e->pins == 0 && !victim) victim = e;
    }

    if (victim) {
        if (victim->dirty) {
            if (victim->dev->write(victim->dev, victim->lba, 1, victim->data) == 0) {
                g_writes++;
            } else {
                serial_emergency_print("bcache: evict write failed\n");
            }
        }
        g_evictions++;
        victim->dev = NULL;
        victim->lba = 0;
        victim->dirty = 0;
        victim->pins = 0;
        return victim;
    }

    return NULL;
}

static int load_entry(BCacheEntry* e, BlockDevice* dev, uint64_t lba) {
    if (dev->read(dev, lba, 1, e->data) != 0) {
        serial_emergency_print("bcache: disk read failed lba=");
        serial_emergency_print_hex(lba);
        serial_emergency_print("\n");
        return -1;
    }
    e->dev = dev;
    e->lba = lba;
    e->dirty = 0;
    e->pins = 0;
    return 0;
}

const uint8_t* bcache_get(BlockDevice* dev, uint64_t lba) {
    #ifdef DEBUG
    serial_emergency_print("bcache_get: dev=");
    serial_emergency_print_hex((uint64_t)(uintptr_t)dev);
    serial_emergency_print(" lba=");
    serial_emergency_print_hex(lba);
    serial_emergency_print("\n");
    #endif
    uint64_t f = bcache_irq_save();

    BCacheEntry* e = find_entry(dev, lba);
    if (e) {
        #ifdef DEBUG
        serial_emergency_print("  → hit\n");
        #endif
        e->pins++;
        g_hits++;
        bcache_irq_restore(f);
        return e->data;
    }
    #ifdef DEBUG
    serial_emergency_print("  → miss\n");
    #endif
    e = alloc_entry(dev, lba);
    if (!e) {
        #ifdef DEBUG
        serial_emergency_print("  → alloc_entry FAILED\n");
        #endif
        bcache_irq_restore(f);
        return NULL;
    }
    #ifdef DEBUG
    serial_emergency_print("  → slot=");
    serial_emergency_print_hex((uint64_t)(e - g_cache));
    serial_emergency_print(" calling load_entry\n");
    #endif

    if (load_entry(e, dev, lba) != 0) {
        #ifdef DEBUG
        serial_emergency_print("  → load_entry FAILED\n");
        #endif
        bcache_irq_restore(f);
        return NULL;
    }
    
    #ifdef DEBUG
    serial_emergency_print("  → loaded, first 8 bytes: ");
    
    for (int i = 0; i < 8; i++) {
        serial_emergency_print_hex(e->data[i]);
        serial_emergency_print(" ");
    }
    serial_emergency_print("\n");
    #endif

    e->pins = 1;
    bcache_irq_restore(f);
    return e->data;
}

uint8_t* bcache_get_for_write(BlockDevice* dev, uint64_t lba) {
    uint64_t f = bcache_irq_save();
    BCacheEntry* e = find_entry(dev, lba);
    if (e) {
        e->pins++;
        e->dirty = 1;
        g_hits++;
        return e->data;
    }

    g_misses++;
    e = alloc_entry(dev, lba);
    if (!e) return NULL;

    if (load_entry(e, dev, lba) != 0) return NULL;
    e->pins = 1;
    e->dirty = 1;
    bcache_irq_restore(f);
    return e->data;
}

void bcache_release(const uint8_t* block) {
    uint64_t f = bcache_irq_save();
    for (int i = 0; i < BCACHE_ENTRIES; i++) {
        if (g_cache[i].data == block) {
            if (g_cache[i].pins > 0) g_cache[i].pins--;
            return;
        }
    }
    bcache_irq_restore(f);
}

int bcache_flush(BlockDevice* dev) {
    if (!dev || !dev->write) return -1;
    uint64_t f = bcache_irq_save();
    int n = 0;
    for (int i = 0; i < BCACHE_ENTRIES; i++) {
        BCacheEntry* e = &g_cache[i];
        if (e->dev != dev || !e->dirty) continue;
        if (dev->write(dev, e->lba, 1, e->data) != 0) { 
            bcache_irq_restore(f);
            return -1;
        }
        g_writes++;
        n++;
    }

    if (n > 0 && dev->flush && dev->flush(dev) != 0) { 
        bcache_irq_restore(f);
        return -1;
    }

    for (int i = 0; i < BCACHE_ENTRIES; i++) {
        BCacheEntry* e = &g_cache[i];
        if (e->dev == dev) e->dirty = 0;
    }
    bcache_irq_restore(f);
    return n;
}

void bcache_sync(BlockDevice* dev) {
    uint64_t f = bcache_irq_save();
    bcache_flush(dev);
    for (int i = 0; i < BCACHE_ENTRIES; i++) {
        if (g_cache[i].dev == dev && g_cache[i].pins == 0) {
            g_cache[i].dev = NULL;
            g_cache[i].lba = 0;
            g_cache[i].dirty = 0;
        }
    }
    bcache_irq_restore(f);
}

void bcache_stats(void) {
    serial_emergency_print("bcache: hits=");
    serial_emergency_print_hex(g_hits);
    serial_emergency_print(" misses=");
    serial_emergency_print_hex(g_misses);
    serial_emergency_print(" evictions=");
    serial_emergency_print_hex(g_evictions);
    serial_emergency_print(" writes=");
    serial_emergency_print_hex(g_writes);
    serial_emergency_print("\n");
}