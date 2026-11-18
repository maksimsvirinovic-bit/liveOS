#ifndef OPS_DISK_H
#define OPS_DISK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define DISK_MAX  8

typedef struct {
    const char* name;
    uint32_t    sector_size;
    uint64_t    sector_count;
    uint64_t    size_bytes;
    int (*read_cb) (uint64_t lba, uint32_t count, void* buf);
    int (*write_cb)(uint64_t lba, uint32_t count, const void* buf);
    int (*flush_cb)(void);
    void* drv_data;
} Disk;

int  disk_subsystem_init(void);

int  disk_register(Disk* disk);

int  disk_count(void);

const Disk* disk_get(int idx);

int  disk_read (int idx, uint64_t lba, uint32_t count, void* buf);
int  disk_write(int idx, uint64_t lba, uint32_t count, const void* buf);
int  disk_flush(int idx);

#ifdef __cplusplus
}
#endif

#endif