#ifndef FAT32_H
#define FAT32_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "block.h"
#include "fs.h"

#define FAT32_ATTR_READ_ONLY  0x01
#define FAT32_ATTR_HIDDEN     0x02
#define FAT32_ATTR_SYSTEM     0x04
#define FAT32_ATTR_VOLUME_ID  0x08
#define FAT32_ATTR_DIRECTORY  0x10
#define FAT32_ATTR_ARCHIVE    0x20
#define FAT32_ATTR_LFN        0x0F

#define FAT32_CLUSTER_FREE    0x00000000
#define FAT32_CLUSTER_BAD     0x0FFFFFF7
#define FAT32_CLUSTER_EOC     0x0FFFFFF8
#define FAT32_CLUSTER_MASK    0x0FFFFFFF

typedef struct {
    uint8_t  jump[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fsinfo_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_num;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
    uint8_t  boot_code[420];
    uint16_t signature;
} __attribute__((packed)) Fat32BootSector;

typedef struct {
    char     name[11];
    uint8_t  attr;
    uint8_t  nt_reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_low;
    uint32_t file_size;
} __attribute__((packed)) Fat32DirEntry;

typedef struct Fat32 {
    BlockDevice* dev;
    uint32_t     bytes_per_sector;
    uint32_t     sectors_per_cluster;
    uint32_t     cluster_size;
    uint32_t     reserved_sectors;
    uint32_t     num_fats;
    uint32_t     fat_start_lba;
    uint32_t     fat_sectors;
    uint32_t     data_start_lba;
    uint32_t     root_cluster;
    uint32_t     total_clusters;
} Fat32;

typedef struct {
    uint32_t cluster;
    Fat32DirEntry entry;
    int is_file;
} PathResult;

typedef struct {
    uint32_t dir_cluster;
    Fat32DirEntry entry;
    uint64_t pos;
} Fat32FileHandle;

int      fat32_mount(BlockDevice* dev, FileSystem** out);
void     fat32_umount(FileSystem* fs);

uint32_t fat32_next_cluster(Fat32* fs, uint32_t cluster);
int      fat32_set_cluster(Fat32* fs, uint32_t cluster, uint32_t value);
int      fat32_alloc_cluster(Fat32* fs, uint32_t prev, uint32_t* out);
int      fat32_free_chain(Fat32* fs, uint32_t start);

int      fat32_read_cluster(Fat32* fs, uint32_t cluster, void* buf);
int      fat32_write_cluster(Fat32* fs, uint32_t cluster, const void* buf);
uint32_t fat32_dir_cluster(Fat32DirEntry* e);
uint64_t fat32_cluster_to_lba(Fat32* fs, uint32_t cluster);

int fat32_create_entry(Fat32* fs, uint32_t dir_cluster, const Fat32DirEntry* entry);
int fat32_update_entry(Fat32* fs, uint32_t dir_cluster, const char* name, Fat32DirEntry* entry);

int fat32_lookup(Fat32* fs, uint32_t dir_cluster, const char* name, Fat32DirEntry* out);
int fat32_readdir(Fat32* fs, uint32_t dir_cluster, uint32_t index, Fat32DirEntry* out);
int fat32_read_file(Fat32* fs, Fat32DirEntry* entry, uint64_t offset, void* buf, uint64_t size);

int fat32_resolve(Fat32* fs, uint32_t cwd, const char* input, PathResult* out);

int fat32_write_file(Fat32* fs, Fat32DirEntry* entry, uint32_t dir_cluster,
                     uint64_t offset, const void* buf, uint64_t size);
int fat32_create(Fat32* fs, uint32_t dir_cluster, const char* name, Fat32DirEntry* out);
int fat32_mkdir(Fat32* fs, uint32_t dir_cluster, const char* name, Fat32DirEntry* out);
int fat32_unlink(Fat32* fs, uint32_t dir_cluster, const char* name);
int fat32_rmdir(Fat32* fs, uint32_t dir_cluster, const char* name);
int fat32_rmdir_r(Fat32* fs, uint32_t dir_cluster, const char* name);

void     fat32_init(void);

#ifdef __cplusplus
}
#endif

#endif