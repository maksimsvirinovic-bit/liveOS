#include "fat32.h"
#include "block_cache.h"
#include "fs.h"
#include "heap.h"
#include "pmm.h"
#include "paging.h"
#include "serial/serial.h"
#include "tasking/process.h"
#include <str.h>
#include <stddef.h>
#include <stdio.h>

int fat32_mount(BlockDevice* dev, FileSystem** out) {
    if (!dev) return -1;

    const uint8_t* block = bcache_get(dev, 0);
    BlockDevice* dev_ = block_find("nvme0");
    if (!dev_) {
        printf("test: no nvme0\n");
        return -1;
    }

    printf("test: nvme0 sector_size=%u count=%lu\n",
           dev_->sector_size, dev_->sector_count);

    uint64_t phys = pmm_alloc_page();
    if (!phys) { printf("test: no phys\n"); return -1; }
    uint8_t* buf = (uint8_t*)phys_to_virt(phys);
    for (int i = 0; i < 512; i++) buf[i] = 0xCC;

    int rc = dev_->read(dev_, 0, 1, buf);
    printf("test: dev->read(0) = %d\n", rc);

    printf("test: first 16 bytes: ");
    for (int i = 0; i < 16; i++) printf("%02x ", buf[i]);
    printf("\n");

    printf("test: bytes 510-511: %02x %02x (signature=0x%04x)\n",
           buf[510], buf[511], *(uint16_t*)(buf + 510));

    pmm_free_page(phys);

    if (!block) {
        serial_emergency_print("fat32: cannot read boot sector\n");
        return -1;
    }

    Fat32BootSector* bs = (Fat32BootSector*)block;

    if (bs->signature != 0xAA55) {
        serial_emergency_print("fat32: bad signature 0x");
        serial_emergency_print_hex(bs->signature);
        serial_emergency_print("\n");
        bcache_release((uint8_t*)block);
        return -1;
    }

    if (strncmp(bs->fs_type, "FAT32", 5) != 0) {
        serial_emergency_print("fat32: not FAT32 (type=");
        for (int i = 0; i < 8; i++) serial_emergency_putc(bs->fs_type[i]);
        serial_emergency_print(")\n");
        bcache_release((uint8_t*)block);
        return -1;
    }

    uint32_t total_sectors = bs->total_sectors_32;
    uint64_t fat_area_sectors = (uint64_t)bs->num_fats * bs->fat_size_32;
    uint64_t data_start = (uint64_t)bs->reserved_sectors + fat_area_sectors;
    if (bs->bytes_per_sector != dev->sector_size ||
        bs->bytes_per_sector < 512 || bs->bytes_per_sector > 4096 ||
        (bs->bytes_per_sector & (bs->bytes_per_sector - 1)) != 0 ||
        bs->sectors_per_cluster == 0 ||
        (bs->sectors_per_cluster & (bs->sectors_per_cluster - 1)) != 0 ||
        (uint64_t)bs->bytes_per_sector * bs->sectors_per_cluster > 128 * 1024 ||
        bs->reserved_sectors == 0 || bs->num_fats == 0 || bs->num_fats > 2 ||
        bs->fat_size_32 == 0 || data_start >= total_sectors ||
        total_sectors > dev->sector_count || bs->root_cluster < 2) {
        serial_emergency_print("fat32: invalid BPB geometry\n");
        bcache_release((uint8_t*)block);
        return -1;
    }

    Fat32* fs = (Fat32*)kzalloc(sizeof(Fat32));
    if (!fs) { bcache_release((uint8_t*)block); return -1; }

    fs->dev = dev;
    fs->bytes_per_sector   = bs->bytes_per_sector;
    fs->sectors_per_cluster = bs->sectors_per_cluster;
    fs->cluster_size       = fs->bytes_per_sector * fs->sectors_per_cluster;
    fs->reserved_sectors   = bs->reserved_sectors;
    fs->num_fats           = bs->num_fats;
    fs->root_cluster       = bs->root_cluster;
    fs->fat_sectors        = bs->fat_size_32;

    fs->fat_start_lba  = bs->reserved_sectors;
    fs->data_start_lba = (uint32_t)data_start;

    uint32_t data_sectors = total_sectors - fs->data_start_lba;
    fs->total_clusters = data_sectors / fs->sectors_per_cluster;
    if (!fs->total_clusters || fs->root_cluster >= fs->total_clusters + 2 ||
        fs->fat_sectors * (uint64_t)fs->bytes_per_sector / 4 < fs->total_clusters + 2) {
        serial_emergency_print("fat32: invalid cluster geometry\n");
        kfree(fs);
        bcache_release((uint8_t*)block);
        return -1;
    }

    serial_emergency_print("fat32: mounted, bps=");
    serial_emergency_print_hex(fs->bytes_per_sector);
    serial_emergency_print(" spc=");
    serial_emergency_print_hex(fs->sectors_per_cluster);
    serial_emergency_print(" cluster=");
    serial_emergency_print_hex(fs->cluster_size);
    serial_emergency_print(" root=");
    serial_emergency_print_hex(fs->root_cluster);
    serial_emergency_print(" clusters=");
    serial_emergency_print_hex(fs->total_clusters);
    serial_emergency_print("\n");

    bcache_release((uint8_t*)block);

    (*out)->private = fs;
    (*out)->cwd_cluster = fs->root_cluster;
    return 0;
}

void fat32_umount(FileSystem* fs) {
    if (!fs || !fs->private) return;
    bcache_flush(fs->dev);
    kfree(fs->private);
    fs->private = NULL;
}

uint64_t fat32_cluster_to_lba(Fat32* fs, uint32_t cluster) {
    return fs->data_start_lba + (uint64_t)(cluster - 2) * fs->sectors_per_cluster;
}

uint32_t fat32_next_cluster(Fat32* fs, uint32_t cluster) {
    if (!fs || cluster < 2 || cluster >= fs->total_clusters + 2) return FAT32_CLUSTER_EOC;
    uint64_t byte_offset = (uint64_t)cluster * 4;
    uint64_t sector = fs->fat_start_lba + (byte_offset / fs->bytes_per_sector);
    uint64_t offset = byte_offset % fs->bytes_per_sector;

    const uint8_t* block = bcache_get(fs->dev, sector);
    if (!block) return FAT32_CLUSTER_EOC;

    uint32_t next = *(uint32_t*)(block + offset) & FAT32_CLUSTER_MASK;
    bcache_release((uint8_t*)block);
    return next;
}

int fat32_set_cluster(Fat32* fs, uint32_t cluster, uint32_t value) {
    if (!fs || cluster < 2 || cluster >= fs->total_clusters + 2 ||
        value > FAT32_CLUSTER_MASK) return -1;
    uint64_t byte_offset = (uint64_t)cluster * 4;
    uint64_t sector = fs->fat_start_lba + (byte_offset / fs->bytes_per_sector);
    uint64_t offset = byte_offset % fs->bytes_per_sector;

    uint8_t* block = bcache_get_for_write(fs->dev, sector);
    if (!block) return -1;

    uint32_t old = *(uint32_t*)(block + offset);
    old = (old & 0xF0000000) | (value & FAT32_CLUSTER_MASK);
    *(uint32_t*)(block + offset) = old;

    bcache_release(block);
    return 0;
}

int fat32_alloc_cluster(Fat32* fs, uint32_t prev, uint32_t* out) {
    if (!fs || !out || (prev && (prev < 2 || prev >= fs->total_clusters + 2))) return -1;
    for (uint32_t c = 2; c < fs->total_clusters + 2; c++) {
        if (fat32_next_cluster(fs, c) == FAT32_CLUSTER_FREE) {
            if (fat32_set_cluster(fs, c, FAT32_CLUSTER_EOC) != 0) return -1;
            if (prev != 0 && fat32_set_cluster(fs, prev, c) != 0) {
                fat32_set_cluster(fs, c, FAT32_CLUSTER_FREE);
                return -1;
            }
            *out = c;
            return 0;
        }
    }
    serial_emergency_print("fat32: no free clusters\n");
    return -1;
}

int fat32_free_chain(Fat32* fs, uint32_t start) {
    uint32_t c = start;
    uint32_t visited = 0;
    while (fs && c >= 2 && c < fs->total_clusters + 2 && visited++ < fs->total_clusters) {
        uint32_t next = fat32_next_cluster(fs, c);
        fat32_set_cluster(fs, c, FAT32_CLUSTER_FREE);
        if (next >= FAT32_CLUSTER_EOC) break;
        if (next < 2 || next >= fs->total_clusters + 2) return -1;
        c = next;
    }
    return !fs || visited >= fs->total_clusters ? -1 : 0;
}

int fat32_read_cluster(Fat32* fs, uint32_t cluster, void* buf) {
    if (!fs || !buf || cluster < 2 || cluster >= fs->total_clusters + 2) return -1;
    uint64_t lba = fat32_cluster_to_lba(fs, cluster);
    uint8_t* dst = (uint8_t*)buf;
    for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
        const uint8_t* block = bcache_get(fs->dev, lba + s);
        if (!block) return -1;
        memcpy(dst + s * fs->bytes_per_sector, block, fs->bytes_per_sector);
        bcache_release((uint8_t*)block);
    }
    return 0;
}

int fat32_write_cluster(Fat32* fs, uint32_t cluster, const void* buf) {
    if (!fs || !buf || cluster < 2 || cluster >= fs->total_clusters + 2) return -1;
    uint64_t lba = fat32_cluster_to_lba(fs, cluster);
    const uint8_t* src = (const uint8_t*)buf;
    for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
        uint8_t* block = bcache_get_for_write(fs->dev, lba + s);
        if (!block) return -1;
        memcpy(block, src + s * fs->bytes_per_sector, fs->bytes_per_sector);
        bcache_release(block);
    }
    return 0;
}

uint32_t fat32_dir_cluster(Fat32DirEntry* e) {
    return ((uint32_t)e->cluster_high << 16) | e->cluster_low;
}

static void to_83(const char* name, char out[11]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    if (strcmp(name, ".") == 0) {
        out[0] = '.';
        return;
    }
    if (strcmp(name, "..") == 0) {
        out[0] = '.'; out[1] = '.';
        return;
    }
    int i = 0, j = 0;
    while (name[i] && name[i] != '.' && j < 8) {
        char c = name[i++];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[j++] = c;
    }
    if (name[i] == '.') {
        i++;
        j = 8;
        while (name[i] && j < 11) {
            char c = name[i++];
            if (c >= 'a' && c <= 'z') c -= 32;
            out[j++] = c;
        }
    }
}

int fat32_lookup(Fat32* fs, uint32_t dir_cluster, const char* name, Fat32DirEntry* out) {
    char target[11];
    to_83(name, target);

    uint32_t cluster = dir_cluster;
    uint8_t* dir_buf = (uint8_t*)kmalloc(fs->cluster_size);
    if (!dir_buf) return -1;

    while (cluster >= 2 && cluster < fs->total_clusters + 2) {
        if (fat32_read_cluster(fs, cluster, dir_buf) != 0) break;

        uint32_t entries = fs->cluster_size / sizeof(Fat32DirEntry);
        for (uint32_t i = 0; i < entries; i++) {
            Fat32DirEntry* e = (Fat32DirEntry*)(dir_buf + i * sizeof(Fat32DirEntry));
            if (e->name[0] == 0x00) {
                kfree(dir_buf);
                return -1;
            }
            if (e->name[0] == (char)0xE5) continue;
            if (e->attr == FAT32_ATTR_LFN) continue;
            if (e->attr & FAT32_ATTR_VOLUME_ID) continue;

            if (memcmp(e->name, target, 11) == 0) {
                memcpy(out, e, sizeof(Fat32DirEntry));
                kfree(dir_buf);
                return 0;
            }
        }
        cluster = fat32_next_cluster(fs, cluster);
    }

    kfree(dir_buf);
    return -1;
}

int fat32_readdir(Fat32* fs, uint32_t dir_cluster, uint32_t index, Fat32DirEntry* out) {
    uint32_t cluster = dir_cluster;
    uint8_t* dir_buf = (uint8_t*)kmalloc(fs->cluster_size);
    if (!dir_buf) return -1;

    uint32_t global_index = 0;

    while (cluster >= 2 && cluster < fs->total_clusters + 2) {
        if (fat32_read_cluster(fs, cluster, dir_buf) != 0) break;

        uint32_t entries = fs->cluster_size / sizeof(Fat32DirEntry);
        for (uint32_t i = 0; i < entries; i++) {
            Fat32DirEntry* e = (Fat32DirEntry*)(dir_buf + i * sizeof(Fat32DirEntry));
            if (e->name[0] == 0x00) { kfree(dir_buf); return -1; }
            if (e->name[0] == (char)0xE5) continue;
            if (e->attr == FAT32_ATTR_LFN) continue;
            if (e->attr & FAT32_ATTR_VOLUME_ID) continue;

            if (global_index == index) {
                memcpy(out, e, sizeof(Fat32DirEntry));
                kfree(dir_buf);
                return 0;
            }
            global_index++;
        }
        cluster = fat32_next_cluster(fs, cluster);
    }

    kfree(dir_buf);
    return -1;
}

int fat32_read_file(Fat32* fs, Fat32DirEntry* entry, uint64_t offset, void* buf, uint64_t size) {
    if (offset >= entry->file_size) return 0;
    uint64_t remaining = entry->file_size - offset;
    if (size > remaining) size = remaining;
    if (size == 0) return 0;

    uint32_t cluster = fat32_dir_cluster(entry);
    uint64_t skip = offset / fs->cluster_size;
    uint64_t in_cluster = offset % fs->cluster_size;

    while (skip > 0 && cluster >= 2 && cluster < fs->total_clusters + 2) {
        cluster = fat32_next_cluster(fs, cluster);
        skip--;
    }

    uint8_t* cluster_buf = (uint8_t*)kmalloc(fs->cluster_size);
    if (!cluster_buf) return -1;

    uint8_t* dst = (uint8_t*)buf;
    uint64_t copied = 0;

    while (copied < size && cluster >= 2 && cluster < fs->total_clusters + 2) {
        if (fat32_read_cluster(fs, cluster, cluster_buf) != 0) break;
        uint64_t to_copy = fs->cluster_size - in_cluster;
        if (to_copy > size - copied) to_copy = size - copied;
        memcpy(dst + copied, cluster_buf + in_cluster, to_copy);
        copied += to_copy;
        in_cluster = 0;
        if (copied < size) cluster = fat32_next_cluster(fs, cluster);
    }

    kfree(cluster_buf);
    return (int)copied;
}

int fat32_resolve(Fat32* fs, uint32_t cwd, const char* input, PathResult* out) {
    if (!fs || !input || !out) return -1;

    uint32_t current_cluster = (input[0] == '/') ? fs->root_cluster : cwd;
    int is_root = (current_cluster == fs->root_cluster);

    const char* p = input;
    if (*p == '/') p++;

    char component[256];

    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        int i = 0;
        while (*p && *p != '/' && i < 255) {
            component[i++] = *p++;
        }
        component[i] = '\0';
        if (i == 0) break;

        if (component[0] == '.' && component[1] == '\0') {
            continue;
        }
        if (component[0] == '.' && component[1] == '.' && component[2] == '\0') {
            if (is_root) {
                continue;
            }
            Fat32DirEntry dotdot;
            if (fat32_lookup(fs, current_cluster, "..", &dotdot) != 0) {
                return -1;
            }
            uint32_t parent = fat32_dir_cluster(&dotdot);
            if (parent == 0) parent = fs->root_cluster;
            current_cluster = parent;
            is_root = (parent == fs->root_cluster);
            continue;
        }

        Fat32DirEntry e;
        if (fat32_lookup(fs, current_cluster, component, &e) != 0) {
            return -1;
        }

        const char* next = p;
        while (*next == '/') next++;
        if (*next == '\0') {
            out->cluster = current_cluster;
            out->entry = e;
            out->is_file = !(e.attr & FAT32_ATTR_DIRECTORY);
            return 0;
        }

        if (!(e.attr & FAT32_ATTR_DIRECTORY)) {
            return -1;
        }
        current_cluster = fat32_dir_cluster(&e);
        is_root = (current_cluster == fs->root_cluster);
    }

    out->cluster = current_cluster;
    out->entry.attr = FAT32_ATTR_DIRECTORY;
    out->entry.cluster_high = (current_cluster >> 16) & 0xFFFF;
    out->entry.cluster_low = current_cluster & 0xFFFF;
    out->entry.file_size = 0;
    out->is_file = 0;
    return 0;
}

int fat32_create_entry(Fat32* fs, uint32_t dir_cluster, const Fat32DirEntry* entry) {
    if (!fs || !entry || dir_cluster < 2 || dir_cluster >= fs->total_clusters + 2) return -1;

    uint32_t cluster = dir_cluster;
    uint8_t* dir_buf = (uint8_t*)kmalloc(fs->cluster_size);
    if (!dir_buf) return -1;

    while (cluster >= 2 && cluster < fs->total_clusters + 2) {
        if (fat32_read_cluster(fs, cluster, dir_buf) != 0) break;

        uint32_t entries = fs->cluster_size / sizeof(Fat32DirEntry);
        for (uint32_t i = 0; i < entries; i++) {
            Fat32DirEntry* e = (Fat32DirEntry*)(dir_buf + i * sizeof(Fat32DirEntry));
            if (e->name[0] == 0x00 || e->name[0] == (char)0xE5) {
                memcpy(e, entry, sizeof(Fat32DirEntry));
                int rc = fat32_write_cluster(fs, cluster, dir_buf);
                kfree(dir_buf);
                return rc;
            }
        }
        uint32_t next = fat32_next_cluster(fs, cluster);
        if (next >= FAT32_CLUSTER_EOC || next < 2 || next >= fs->total_clusters + 2) {
            uint32_t new_cluster;
            if (fat32_alloc_cluster(fs, cluster, &new_cluster) != 0) break;

            memset(dir_buf, 0, fs->cluster_size);
            memcpy(dir_buf, entry, sizeof(Fat32DirEntry));
            int rc = fat32_write_cluster(fs, new_cluster, dir_buf);
            if (rc != 0) {
                fat32_set_cluster(fs, cluster, FAT32_CLUSTER_EOC);
                fat32_free_chain(fs, new_cluster);
            }
            kfree(dir_buf);
            return rc;
        }
        cluster = next;
    }

    kfree(dir_buf);
    return -1;
}

int fat32_update_entry(Fat32* fs, uint32_t dir_cluster, const char* name,
                              Fat32DirEntry* entry) {
    (void)name;
    if (!fs || !entry) return -1;
    char target[11];
    memcpy(target, entry->name, sizeof(target));

    uint32_t cluster = dir_cluster;
    uint8_t* dir_buf = (uint8_t*)kmalloc(fs->cluster_size);
    if (!dir_buf) return -1;

    while (cluster >= 2 && cluster < fs->total_clusters + 2) {
        if (fat32_read_cluster(fs, cluster, dir_buf) != 0) break;

        uint32_t entries = fs->cluster_size / sizeof(Fat32DirEntry);
        for (uint32_t i = 0; i < entries; i++) {
            Fat32DirEntry* e = (Fat32DirEntry*)(dir_buf + i * sizeof(Fat32DirEntry));
            if (e->name[0] == 0x00) { kfree(dir_buf); return -1; }
            if (e->name[0] == (char)0xE5) continue;
            if (e->attr == FAT32_ATTR_LFN) continue;
            if (memcmp(e->name, target, 11) == 0) {
                memcpy(e, entry, sizeof(Fat32DirEntry));
                int rc = fat32_write_cluster(fs, cluster, dir_buf);
                kfree(dir_buf);
                return rc;
            }
        }
        cluster = fat32_next_cluster(fs, cluster);
    }

    kfree(dir_buf);
    return -1;
}

int fat32_write_file(Fat32* fs, Fat32DirEntry* entry, uint32_t dir_cluster,
                     uint64_t offset, const void* buf, uint64_t size) {
    if (!fs || !entry || size == 0) return 0;

    uint32_t cluster = fat32_dir_cluster(entry);
    uint32_t cluster_count = (entry->file_size + fs->cluster_size - 1) / fs->cluster_size;
    uint64_t end_offset = offset + size;
    uint32_t needed_clusters = (end_offset + fs->cluster_size - 1) / fs->cluster_size;

    if (cluster == 0) {
        if (fat32_alloc_cluster(fs, 0, &cluster) != 0) return -1;
        cluster_count = 1;
    }

    while (cluster_count < needed_clusters) {
        uint32_t tail = cluster;
        uint32_t next = fat32_next_cluster(fs, tail);
        while (next < FAT32_CLUSTER_EOC) {
            tail = next;
            next = fat32_next_cluster(fs, tail);
        }
        uint32_t new_cluster;
        if (fat32_alloc_cluster(fs, tail, &new_cluster) != 0) return -1;
        cluster_count++;
    }

    uint64_t skip = offset / fs->cluster_size;
    uint64_t in_cluster = offset % fs->cluster_size;
    uint32_t c = cluster;
    for (uint64_t k = 0; k < skip; k++) {
        c = fat32_next_cluster(fs, c);
    }

    uint8_t* cluster_buf = (uint8_t*)kmalloc(fs->cluster_size);
    if (!cluster_buf) return -1;

    uint64_t written = 0;
    while (written < size && c >= 2 && c < fs->total_clusters + 2) {
        if (fat32_read_cluster(fs, c, cluster_buf) != 0) break;
        uint64_t to_copy = fs->cluster_size - in_cluster;
        if (to_copy > size - written) to_copy = size - written;
        memcpy(cluster_buf + in_cluster, (const uint8_t*)buf + written, to_copy);
        if (fat32_write_cluster(fs, c, cluster_buf) != 0) break;
        written += to_copy;
        in_cluster = 0;
        if (written < size) c = fat32_next_cluster(fs, c);
    }

    kfree(cluster_buf);

    entry->cluster_high = (cluster >> 16) & 0xFFFF;
    entry->cluster_low = cluster & 0xFFFF;
    if (end_offset > entry->file_size) entry->file_size = end_offset;

    // TODO: find_entry_offset(dir_cluster, name) → offset; write entry.

    return (int)written;
}

int fat32_create(Fat32* fs, uint32_t dir_cluster, const char* name, Fat32DirEntry* out) {
    Fat32DirEntry entry;
    memset(&entry, 0, sizeof(entry));
    to_83(name, entry.name);
    entry.attr = FAT32_ATTR_ARCHIVE;
    entry.file_size = 0;

    if (fat32_create_entry(fs, dir_cluster, &entry) != 0) return -1;
    if (out) *out = entry;
    return 0;
}

int fat32_mkdir(Fat32* fs, uint32_t dir_cluster, const char* name, Fat32DirEntry* out) {
    uint32_t new_cluster;
    if (fat32_alloc_cluster(fs, 0, &new_cluster) != 0) {
        serial_emergency_print("fat32_mkdir: no free cluster\n");
        return -1;
    }

    Fat32DirEntry entry;
    memset(&entry, 0, sizeof(entry));
    to_83(name, entry.name);
    entry.attr = FAT32_ATTR_DIRECTORY;
    entry.cluster_high = (new_cluster >> 16) & 0xFFFF;
    entry.cluster_low  = new_cluster & 0xFFFF;
    entry.file_size = 0;

    if (fat32_create_entry(fs, dir_cluster, &entry) != 0) {
        fat32_free_chain(fs, new_cluster);
        return -1;
    }

    uint8_t* cluster_buf = (uint8_t*)kzalloc(fs->cluster_size);
    if (!cluster_buf) {
        fat32_free_chain(fs, new_cluster);
        return -1;
    }

    Fat32DirEntry* dot = (Fat32DirEntry*)cluster_buf;
    memset(dot, 0, sizeof(*dot));
    dot->name[0] = '.';
    for (int i = 1; i < 11; i++) dot->name[i] = ' ';
    dot->attr = FAT32_ATTR_DIRECTORY;
    dot->cluster_high = (new_cluster >> 16) & 0xFFFF;
    dot->cluster_low  = new_cluster & 0xFFFF;

    Fat32DirEntry* dotdot = (Fat32DirEntry*)(cluster_buf + sizeof(Fat32DirEntry));
    memset(dotdot, 0, sizeof(*dotdot));
    dotdot->name[0] = '.'; dotdot->name[1] = '.';
    for (int i = 2; i < 11; i++) dotdot->name[i] = ' ';
    dotdot->attr = FAT32_ATTR_DIRECTORY;
    uint32_t parent = (dir_cluster == fs->root_cluster) ? 0 : dir_cluster;
    dotdot->cluster_high = (parent >> 16) & 0xFFFF;
    dotdot->cluster_low  = parent & 0xFFFF;

    if (fat32_write_cluster(fs, new_cluster, cluster_buf) != 0) {
        kfree(cluster_buf);
        fat32_free_chain(fs, new_cluster);
        return -1;
    }
    kfree(cluster_buf);

    if (out) *out = entry;
    return 0;
}

static int64_t fat32_file_read(File* f, void* buf, uint64_t count) {
    Fat32FileHandle* h = (Fat32FileHandle*)f->private;
    Fat32* fs = (Fat32*)f->fs->private;

    if (h->pos >= h->entry.file_size) return 0;
    uint64_t remaining = h->entry.file_size - h->pos;
    if (count > remaining) count = remaining;

    int n = fat32_read_file(fs, &h->entry, h->pos, buf, count);
    if (n > 0) h->pos += n;
    return n;
}

static int64_t fat32_file_write(File* f, const void* buf, uint64_t count) {
    Fat32FileHandle* h = (Fat32FileHandle*)f->private;
    Fat32* fs = (Fat32*)f->fs->private;

    int n = fat32_write_file(fs, &h->entry, h->dir_cluster, h->pos, buf, count);
    if (n > 0) h->pos += n;
    fat32_update_entry(fs, h->dir_cluster, h->entry.name, &h->entry);
    f->size = h->entry.file_size;
    return n;
}

static int64_t fat32_file_seek(File* f, int64_t offset, int whence) {
    Fat32FileHandle* h = (Fat32FileHandle*)f->private;
    int64_t base = 0;
    if (whence == SEEK_CUR) base = h->pos;
    else if (whence == SEEK_END) base = h->entry.file_size;
    int64_t newpos = base + offset;
    if (newpos < 0) newpos = 0;
    if (newpos > (int64_t)h->entry.file_size) newpos = h->entry.file_size;
    h->pos = newpos;
    return newpos;
}

static int fat32_file_close(File* f) {
    if (!f) return -1;
    int result = bcache_flush(f->fs->dev) < 0 ? -1 : 0;
    if (f->private) kfree(f->private);
    if (f->path) kfree(f->path);
    kfree(f);
    return result;
}

static uint32_t fat32_process_cwd(Fat32* fs) {
    Process* p = process_get_current();
    return p && p->cwd_cluster >= 2 ? p->cwd_cluster : fs->root_cluster;
}

static int fat32_resolve_parent(Fat32* fs, uint32_t cwd, const char* path,
                                uint32_t* parent_cluster, char name[256]) {
    if (!fs || !path || !*path || !parent_cluster || !name) return -1;

    size_t length = strlen(path);
    if (length >= 256) return -1;
    while (length > 1 && path[length - 1] == '/') length--;
    if (length == 0 || (length == 1 && path[0] == '/')) return -1;

    size_t base = length;
    while (base > 0 && path[base - 1] != '/') base--;
    size_t name_length = length - base;
    if (name_length == 0 || name_length >= 256) return -1;
    memcpy(name, path + base, name_length);
    name[name_length] = '\0';
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return -1;

    if (base == 0) {
        *parent_cluster = cwd;
        return 0;
    }

    char parent_path[256];
    size_t parent_length = base;
    while (parent_length > 1 && path[parent_length - 1] == '/') parent_length--;
    if (parent_length == 0) {
        parent_path[0] = '.';
        parent_path[1] = '\0';
    } else {
        memcpy(parent_path, path, parent_length);
        parent_path[parent_length] = '\0';
    }

    PathResult parent;
    if (fat32_resolve(fs, cwd, parent_path, &parent) != 0 || parent.is_file) return -1;
    *parent_cluster = fat32_dir_cluster(&parent.entry);
    if (*parent_cluster < 2 || *parent_cluster >= fs->total_clusters + 2) return -1;
    return 0;
}

static int fat32_open(FileSystem* fs_obj, const char* rel_path, uint32_t flags, File** out) {
    if (!fs_obj || !fs_obj->private || !rel_path || !out) return -1;
    Fat32* fs = (Fat32*)fs_obj->private;
    uint32_t cwd = fat32_process_cwd(fs);

    PathResult pr;
    if (fat32_resolve(fs, cwd, rel_path, &pr) != 0) {
        if (!(flags & FS_CREATE)) return -1;
        char name[256];
        uint32_t dir_cluster;
        if (fat32_resolve_parent(fs, cwd, rel_path, &dir_cluster, name) != 0) return -1;
        if (fat32_create(fs, dir_cluster, name, NULL) != 0) return -1;
        if (fat32_lookup(fs, dir_cluster, name, &pr.entry) != 0) return -1;
        pr.cluster = dir_cluster;
        pr.is_file = 1;
    }

    if ((flags & FS_DIRECTORY) ? pr.is_file : !pr.is_file) return -1;

    if (pr.is_file && (flags & FS_TRUNCATE)) {
        uint32_t old_cluster = fat32_dir_cluster(&pr.entry);
        if (old_cluster >= 2 && fat32_free_chain(fs, old_cluster) != 0) return -1;
        pr.entry.cluster_high = 0;
        pr.entry.cluster_low = 0;
        pr.entry.file_size = 0;
        if (fat32_update_entry(fs, pr.cluster, pr.entry.name, &pr.entry) != 0) return -1;
    }

    File* f = (File*)kzalloc(sizeof(File));
    Fat32FileHandle* h = (Fat32FileHandle*)kzalloc(sizeof(Fat32FileHandle));
    if (!f || !h) { kfree(f); kfree(h); return -1; }
    f->path = (char*)kmalloc(strlen(rel_path) + 1);
    if (!f->path) { kfree(h); kfree(f); return -1; }

    h->dir_cluster = pr.is_file ? pr.cluster : fat32_dir_cluster(&pr.entry);
    h->entry = pr.entry;
    h->pos = 0;

    f->fs = fs_obj;
    strcpy(f->path, rel_path);
    f->size = pr.entry.file_size;
    f->flags = flags;
    f->private = h;
    f->read  = fat32_file_read;
    f->write = fat32_file_write;
    f->seek  = fat32_file_seek;
    f->close = fat32_file_close;

    *out = f;
    return 0;
}

static int fat32_mkdir_vfs(FileSystem* fs_obj, const char* rel_path) {
    if (!fs_obj || !fs_obj->private) return -1;
    Fat32* fs = (Fat32*)fs_obj->private;
    char name[256];
    uint32_t dir_cluster;
    uint32_t cwd = fat32_process_cwd(fs);
    if (fat32_resolve_parent(fs, cwd, rel_path, &dir_cluster, name) != 0) return -1;
    Fat32DirEntry existing;
    int lookup_result = fat32_lookup(fs, dir_cluster, name, &existing);
    if (lookup_result == 0) return -1;
    return fat32_mkdir(fs, dir_cluster, name, NULL);
}

int fat32_unlink(Fat32* fs, uint32_t dir_cluster, const char* name) {
    char target[11];
    to_83(name, target);

    uint32_t cluster = dir_cluster;
    uint8_t* dir_buf = (uint8_t*)kmalloc(fs->cluster_size);
    if (!dir_buf) return -1;

    while (cluster >= 2 && cluster < fs->total_clusters + 2) {
        if (fat32_read_cluster(fs, cluster, dir_buf) != 0) break;

        uint32_t entries = fs->cluster_size / sizeof(Fat32DirEntry);
        for (uint32_t i = 0; i < entries; i++) {
            Fat32DirEntry* e = (Fat32DirEntry*)(dir_buf + i * sizeof(Fat32DirEntry));
            if (e->name[0] == 0x00) { kfree(dir_buf); return -1; }
            if (e->name[0] == (char)0xE5) continue;
            if (e->attr == FAT32_ATTR_LFN) continue;
            if (memcmp(e->name, target, 11) != 0) continue;

            uint32_t first = fat32_dir_cluster(e);
            if (first >= 2) fat32_free_chain(fs, first);

            e->name[0] = 0xE5;
            int rc = fat32_write_cluster(fs, cluster, dir_buf);
            kfree(dir_buf);
            return rc;
        }
        cluster = fat32_next_cluster(fs, cluster);
    }

    kfree(dir_buf);
    return -1;
}

int fat32_rmdir(Fat32* fs, uint32_t dir_cluster, const char* name) {
    char target[11];
    to_83(name, target);

    uint32_t cluster = dir_cluster;
    uint8_t* dir_buf = (uint8_t*)kmalloc(fs->cluster_size);
    if (!dir_buf) return -1;

    while (cluster >= 2 && cluster < fs->total_clusters + 2) {
        if (fat32_read_cluster(fs, cluster, dir_buf) != 0) break;

        uint32_t entries = fs->cluster_size / sizeof(Fat32DirEntry);
        for (uint32_t i = 0; i < entries; i++) {
            Fat32DirEntry* e = (Fat32DirEntry*)(dir_buf + i * sizeof(Fat32DirEntry));
            if (e->name[0] == 0x00) { kfree(dir_buf); return -1; }
            if (e->name[0] == (char)0xE5) continue;
            if (e->attr == FAT32_ATTR_LFN) continue;
            if (memcmp(e->name, target, 11) != 0) continue;

            if (!(e->attr & FAT32_ATTR_DIRECTORY)) { kfree(dir_buf); return -1; }

            uint32_t sub = fat32_dir_cluster(e);
            uint8_t* sub_buf = (uint8_t*)kmalloc(fs->cluster_size);
            if (!sub_buf) { kfree(dir_buf); return -1; }
            int non_empty = 0;
            uint32_t sub_cluster = sub;
            uint32_t visited = 0;
            while (sub_cluster >= 2 && sub_cluster < fs->total_clusters + 2 &&
                   visited++ < fs->total_clusters) {
                if (fat32_read_cluster(fs, sub_cluster, sub_buf) != 0) {
                    kfree(sub_buf);
                    kfree(dir_buf);
                    return -1;
                }

                uint32_t sub_entries = fs->cluster_size / sizeof(Fat32DirEntry);
                int end_of_directory = 0;
                for (uint32_t j = 0; j < sub_entries; j++) {
                    Fat32DirEntry* se = (Fat32DirEntry*)(sub_buf + j * sizeof(Fat32DirEntry));
                    if (se->name[0] == 0x00) {
                        end_of_directory = 1;
                        break;
                    }
                    if (se->name[0] == (char)0xE5 || se->attr == FAT32_ATTR_LFN) continue;
                    if (se->name[0] == '.' && (se->name[1] == ' ' || se->name[1] == '.')) continue;
                    non_empty = 1;
                    break;
                }
                if (non_empty || end_of_directory) break;

                uint32_t next = fat32_next_cluster(fs, sub_cluster);
                if (next >= FAT32_CLUSTER_EOC) break;
                if (next < 2 || next >= fs->total_clusters + 2) {
                    kfree(sub_buf);
                    kfree(dir_buf);
                    return -1;
                }
                sub_cluster = next;
            }
            kfree(sub_buf);
            if (non_empty) { kfree(dir_buf); return -1; }

            if (sub >= 2) fat32_free_chain(fs, sub);

            e->name[0] = 0xE5;
            int rc = fat32_write_cluster(fs, cluster, dir_buf);
            kfree(dir_buf);
            return rc;
        }
        cluster = fat32_next_cluster(fs, cluster);
    }

    kfree(dir_buf);
    return -1;
}

int fat32_rmdir_r(Fat32* fs, uint32_t dir_cluster, const char* name) {
    char target[11];
    to_83(name, target);

    uint32_t cluster = dir_cluster;
    uint8_t* dir_buf = (uint8_t*)kmalloc(fs->cluster_size);
    if (!dir_buf) return -1;

    uint32_t sub_cluster = 0;
    while (cluster >= 2 && cluster < fs->total_clusters + 2) {
        if (fat32_read_cluster(fs, cluster, dir_buf) != 0) break;
        uint32_t entries = fs->cluster_size / sizeof(Fat32DirEntry);
        for (uint32_t i = 0; i < entries; i++) {
            Fat32DirEntry* e = (Fat32DirEntry*)(dir_buf + i * sizeof(Fat32DirEntry));
            if (e->name[0] == 0x00) { kfree(dir_buf); return -1; }
            if (e->name[0] == (char)0xE5) continue;
            if (e->attr == FAT32_ATTR_LFN) continue;
            if (memcmp(e->name, target, 11) != 0) continue;
            if (!(e->attr & FAT32_ATTR_DIRECTORY)) { kfree(dir_buf); return -1; }
            sub_cluster = fat32_dir_cluster(e);
            break;
        }
        if (sub_cluster) break;
        cluster = fat32_next_cluster(fs, cluster);
    }
    kfree(dir_buf);
    if (!sub_cluster) return -1;

    uint8_t* sub_buf = (uint8_t*)kmalloc(fs->cluster_size);
    if (!sub_buf) return -1;
    if (fat32_read_cluster(fs, sub_cluster, sub_buf) != 0) { kfree(sub_buf); return -1; }

    uint32_t sub_entries = fs->cluster_size / sizeof(Fat32DirEntry);
    for (uint32_t j = 0; j < sub_entries; j++) {
        Fat32DirEntry* se = (Fat32DirEntry*)(sub_buf + j * sizeof(Fat32DirEntry));
        if (se->name[0] == 0x00) break;
        if (se->name[0] == (char)0xE5) continue;
        if (se->attr == FAT32_ATTR_LFN) continue;
        if (se->name[0] == '.' && (se->name[1] == ' ' || se->name[1] == '.')) continue;

        char subname[12];
        int k = 0;
        for (int m = 0; m < 8 && se->name[m] != ' '; m++) subname[k++] = se->name[m];
        if (se->name[8] != ' ') {
            subname[k++] = '.';
            for (int m = 8; m < 11 && se->name[m] != ' '; m++) subname[k++] = se->name[m];
        }
        subname[k] = '\0';

        if (se->attr & FAT32_ATTR_DIRECTORY) {
            fat32_rmdir_r(fs, sub_cluster, subname);
        } else {
            fat32_unlink(fs, sub_cluster, subname);
        }
    }
    kfree(sub_buf);

    return fat32_rmdir(fs, dir_cluster, name);
}

static int fat32_unlink_vfs(FileSystem* fs_obj, const char* rel_path) {
    if (!fs_obj || !fs_obj->private) return -1;
    Fat32* fs = (Fat32*)fs_obj->private;
    uint32_t parent_cluster;
    char name[256];
    if (fat32_resolve_parent(fs, fat32_process_cwd(fs), rel_path,
                             &parent_cluster, name) != 0) return -1;

    Fat32DirEntry entry;
    if (fat32_lookup(fs, parent_cluster, name, &entry) != 0 ||
        (entry.attr & FAT32_ATTR_DIRECTORY)) return -1;
    return fat32_unlink(fs, parent_cluster, name);
}

static int fat32_rmdir_vfs(FileSystem* fs_obj, const char* rel_path) {
    if (!fs_obj || !fs_obj->private) return -1;
    Fat32* fs = (Fat32*)fs_obj->private;
    uint32_t parent_cluster;
    char name[256];
    if (fat32_resolve_parent(fs, fat32_process_cwd(fs), rel_path,
                             &parent_cluster, name) != 0) return -1;

    Fat32DirEntry entry;
    if (fat32_lookup(fs, parent_cluster, name, &entry) != 0 ||
        !(entry.attr & FAT32_ATTR_DIRECTORY)) return -1;
    if (fat32_dir_cluster(&entry) == fs->root_cluster) return -1;
    return fat32_rmdir(fs, parent_cluster, name);
}

static int fat32_rmdir_r_vfs(FileSystem* fs_obj, const char* rel_path) {
    if (!fs_obj || !fs_obj->private) return -1;
    Fat32* fs = (Fat32*)fs_obj->private;
    uint32_t parent_cluster;
    char name[256];
    if (fat32_resolve_parent(fs, fat32_process_cwd(fs), rel_path,
                             &parent_cluster, name) != 0) return -1;

    Fat32DirEntry entry;
    if (fat32_lookup(fs, parent_cluster, name, &entry) != 0 ||
        !(entry.attr & FAT32_ATTR_DIRECTORY)) return -1;
    if (fat32_dir_cluster(&entry) == fs->root_cluster) return -1;
    return fat32_rmdir_r(fs, parent_cluster, name);
}

static void fat32_entry_name(const Fat32DirEntry* entry, char* out, size_t out_size) {
    size_t pos = 0;
    for (size_t i = 0; i < 8 && entry->name[i] != ' '; i++) {
        if (pos + 1 >= out_size) break;
        out[pos++] = entry->name[i];
    }
    if (entry->name[8] != ' ' && pos + 1 < out_size) {
        out[pos++] = '.';
        for (size_t i = 8; i < 11 && entry->name[i] != ' ' && pos + 1 < out_size; i++) {
            out[pos++] = entry->name[i];
        }
    }
    out[pos] = '\0';
}

static int fat32_readdir_vfs(FileSystem* fs_obj, File* dir, uint32_t index, DirEntry* out) {
    if (!fs_obj || !fs_obj->private || !dir || dir->fs != fs_obj ||
        !dir->private || !out) return -1;
    Fat32FileHandle* handle = (Fat32FileHandle*)dir->private;
    if (!(handle->entry.attr & FAT32_ATTR_DIRECTORY)) return -1;

    Fat32DirEntry entry;
    if (fat32_readdir((Fat32*)fs_obj->private, handle->dir_cluster, index, &entry) != 0) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    fat32_entry_name(&entry, out->name, sizeof(out->name));
    out->attr = (entry.attr & FAT32_ATTR_DIRECTORY) ? FS_ATTR_DIR : FS_ATTR_FILE;
    if (entry.attr & FAT32_ATTR_READ_ONLY) out->attr |= FS_ATTR_READONLY;
    if (entry.attr & FAT32_ATTR_HIDDEN) out->attr |= FS_ATTR_HIDDEN;
    if (entry.attr & FAT32_ATTR_SYSTEM) out->attr |= FS_ATTR_SYSTEM;
    out->size = entry.file_size;
    return 0;
}

static FSDriver g_fat32_driver = {
    .name    = "fat32",
    .mount   = fat32_mount,
    .umount  = fat32_umount,
    .open    = fat32_open,
    .mkdir   = fat32_mkdir_vfs,
    .unlink  = fat32_unlink_vfs,
    .rmdir   = fat32_rmdir_vfs,
    .rmdir_r = fat32_rmdir_r_vfs,
    .readdir = fat32_readdir_vfs,
    .next    = NULL,
};

void fat32_init(void) {
    fs_register_driver(&g_fat32_driver);
    serial_emergency_print("fat32: registered\n");
}