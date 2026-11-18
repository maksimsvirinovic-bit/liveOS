#ifndef DRIVER_H
#define DRIVER_H

#include <stdint.h>

typedef enum {
    DRIVER_TYPE_CHAR = 0,
    DRIVER_TYPE_BLOCK,
    DRIVER_TYPE_NET,
    DRIVER_TYPE_GRAPHICS,
    DRIVER_TYPE_TIMER,
    DRIVER_TYPE_BUS,
    DRIVER_TYPE_UNKNOWN,
} DriverType;

typedef enum {
    DRIVER_INIT_EARLY = 0,
    DRIVER_INIT_PCI,
    DRIVER_INIT_BUS,
    DRIVER_INIT_BLOCK,
    DRIVER_INIT_FS,
    DRIVER_INIT_NET,
    DRIVER_INIT_LATE,
} DriverInitLevel;

struct Driver;

typedef int  (*DriverInitFn)(struct Driver* self);
typedef void (*DriverDeinitFn)(struct Driver* self);

typedef struct Driver {
    const char*     name;
    DriverType      type;
    DriverInitLevel init_level;
    DriverInitFn    init;
    DriverDeinitFn  deinit;
    void*           private_data;
    struct Driver*  next;
} Driver;

void    driver_subsystem_init(void);
int     driver_register(Driver* drv);
int     driver_unregister(Driver* drv);
Driver* driver_find(const char* name);
void    driver_init_all(void);
void    driver_list(void);

typedef struct {
    void (*putc)(char c);
    int  (*getchar)(void);
    int  (*kbhit)(void);
    void (*set_color)(uint32_t fg, uint32_t bg);
    void (*reset_color)(void);
} CharDriverOps;

CharDriverOps* char_driver_get_primary(void);


#define DISK_MAX  8
#define DISK_SECTOR_SIZE_DEFAULT 512

int disk_init(void);

int disk_count(void);

typedef struct {
    const char* name;
    uint32_t    sector_size;
    uint64_t    sector_count;
    uint64_t    size_bytes;
} DiskInfo;

const DiskInfo* disk_get_info(int idx);

int disk_read (int disk_idx, uint64_t lba, uint32_t count, void* buf);
int disk_write(int disk_idx, uint64_t lba, uint32_t count, const void* buf);
int disk_flush(int disk_idx);

#endif