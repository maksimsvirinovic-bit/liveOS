#ifndef TMPFS_H
#define TMPFS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "block.h"
#include "fs.h"

void tmpfs_init(void);

BlockDevice* ramdisk_create(const char* name, uint64_t sector_count);

void ramdisk_destroy(BlockDevice* dev);

#ifdef __cplusplus
}
#endif

#endif