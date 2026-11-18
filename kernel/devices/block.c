#include "block.h"
#include "heap.h"
#include <serial/serial.h>
#include <stddef.h>
#include <str.h>

static BlockDevice* g_block_devices = NULL;

void block_init(void) {
    g_block_devices = NULL;
}

int block_register(BlockDevice* dev) {
    if (!dev || !dev->name || !dev->read) return -1;
    for (BlockDevice* d = g_block_devices; d; d = d->next) {
        if (strcmp(d->name, dev->name) == 0) return -1;
    }
    dev->next = g_block_devices;
    g_block_devices = dev;
    serial_emergency_print("block: registered ");
    serial_emergency_print(dev->name);
    serial_emergency_print("\n");
    return 0;
}

int block_unregister(BlockDevice* dev) {
    BlockDevice** pp = &g_block_devices;
    while (*pp) {
        if (*pp == dev) { *pp = dev->next; return 0; }
        pp = &(*pp)->next;
    }
    return -1;
}

BlockDevice* block_find(const char* name) {
    for (BlockDevice* d = g_block_devices; d; d = d->next) {
        if (strcmp(d->name, name) == 0) return d;
    }
    return NULL;
}

BlockDevice* block_first(void) { return g_block_devices; }

void block_list(void) {
    serial_emergency_print("Block devices:\n");
    for (BlockDevice* d = g_block_devices; d; d = d->next) {
        serial_emergency_print("  ");
        serial_emergency_print(d->name);
        serial_emergency_print(" sectors=");
        serial_emergency_print_hex(d->sector_count);
        serial_emergency_print("\n");
    }
}