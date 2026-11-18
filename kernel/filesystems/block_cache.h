#ifndef BLOCK_CACHE_H
#define BLOCK_CACHE_H

#include <stdint.h>
#include "block.h"

#define BCACHE_BLOCK_SIZE   4096
#define BCACHE_ENTRIES      256

void bcache_init(void);

const uint8_t* bcache_get(BlockDevice* dev, uint64_t lba);

uint8_t* bcache_get_for_write(BlockDevice* dev, uint64_t lba);

void bcache_release(const uint8_t* block);

int bcache_flush(BlockDevice* dev);

void bcache_sync(BlockDevice* dev);

void bcache_stats(void);

#endif