#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BlockDevice BlockDevice;

struct BlockDevice {
    const char*  name;
    uint32_t     sector_size;
    uint64_t     sector_count;
    void*        private_data;

    int  (*read)  (BlockDevice* dev, uint64_t lba, uint32_t count, void* buf);
    int  (*write) (BlockDevice* dev, uint64_t lba, uint32_t count, const void* buf);
    int  (*flush) (BlockDevice* dev);

    BlockDevice* next;
};

void          block_init(void);
int           block_register(BlockDevice* dev);
int           block_unregister(BlockDevice* dev);
BlockDevice*  block_find(const char* name);
BlockDevice*  block_first(void);
void          block_list(void);

#ifdef __cplusplus
}
#endif

static inline int block_read_bytes(BlockDevice* dev, uint64_t byte_offset,
                                   void* buf, uint64_t size) {
    uint64_t lba = byte_offset / dev->sector_size;
    uint64_t off = byte_offset % dev->sector_size;
    uint64_t sectors = (size + off + dev->sector_size - 1) / dev->sector_size;

    if (off == 0 && (size % dev->sector_size) == 0) {
        return dev->read(dev, lba, (uint32_t)(size / dev->sector_size), buf);
    }

    uint8_t* tmp = (uint8_t*)__builtin_alloca(sectors * dev->sector_size);
    if (dev->read(dev, lba, (uint32_t)sectors, tmp) != 0) return -1;
    uint8_t* dst = (uint8_t*)buf;
    for (uint64_t i = 0; i < size; i++) dst[i] = tmp[off + i];
    return 0;
}

#endif