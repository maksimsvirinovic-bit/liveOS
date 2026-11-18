#include "tmpfs.h"
#include "heap.h"
#include "serial/serial.h"
#include <stddef.h>
#include <str.h>

typedef struct RamDisk {
    uint8_t* data;
    uint64_t size;
} RamDisk;

typedef struct TmpFile {
    char* name;
    uint8_t* data;
    uint64_t size;
    uint64_t capacity;
    struct TmpFile* next;
} TmpFile;

typedef struct TmpFS {
    TmpFile* files;
} TmpFS;

typedef struct {
    TmpFile* file;
    uint64_t pos;
} TmpFileHandle;

static int ramdisk_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* buf) {
    RamDisk* rd = (RamDisk*)dev->private_data;
    uint64_t off = lba * dev->sector_size;
    uint64_t sz = count * dev->sector_size;
    if (off + sz > rd->size) return -1;
    for (uint64_t i = 0; i < sz; i++) ((uint8_t*)buf)[i] = rd->data[off + i];
    return 0;
}

static int ramdisk_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buf) {
    RamDisk* rd = (RamDisk*)dev->private_data;
    uint64_t off = lba * dev->sector_size;
    uint64_t sz = count * dev->sector_size;
    if (off + sz > rd->size) return -1;
    for (uint64_t i = 0; i < sz; i++) rd->data[off + i] = ((const uint8_t*)buf)[i];
    return 0;
}

static int ramdisk_flush(BlockDevice* dev) { (void)dev; return 0; }

BlockDevice* ramdisk_create(const char* name, uint64_t sector_count) {
    RamDisk* rd = (RamDisk*)kmalloc(sizeof(RamDisk));
    if (!rd) return NULL;

    rd->size = sector_count * 512;
    rd->data = (uint8_t*)kmalloc(rd->size);
    if (!rd->data) { kfree(rd); return NULL; }
    for (uint64_t i = 0; i < rd->size; i++) rd->data[i] = 0;

    BlockDevice* dev = (BlockDevice*)kmalloc(sizeof(BlockDevice));
    if (!dev) { kfree(rd->data); kfree(rd); return NULL; }

    dev->name = name;
    dev->sector_size = 512;
    dev->sector_count = sector_count;
    dev->private_data = rd;
    dev->read = ramdisk_read;
    dev->write = ramdisk_write;
    dev->flush = ramdisk_flush;
    dev->next = NULL;

    if (block_register(dev) != 0) {
        kfree(dev); kfree(rd->data); kfree(rd);
        return NULL;
    }
    return dev;
}

void ramdisk_destroy(BlockDevice* dev) {
    if (!dev) return;
    RamDisk* rd = (RamDisk*)dev->private_data;
    block_unregister(dev);
    if (rd) { kfree(rd->data); kfree(rd); }
    kfree(dev);
}

static int tmpfs_mount(BlockDevice* dev, FileSystem** out) {
    (void)dev;
    TmpFS* t = (TmpFS*)kmalloc(sizeof(TmpFS));
    if (!t) return -1;
    t->files = NULL;
    (*out)->private = t;
    return 0;
}

static void tmpfs_umount(FileSystem* fs) {
    TmpFS* t = (TmpFS*)fs->private;
    if (!t) return;
    TmpFile* f = t->files;
    while (f) {
        TmpFile* n = f->next;
        kfree(f->name);
        kfree(f->data);
        kfree(f);
        f = n;
    }
    kfree(t);
}

static TmpFile* tmpfs_find_file(TmpFS* t, const char* name) {
    for (TmpFile* f = t->files; f; f = f->next) {
        if (strcmp(f->name, name) == 0) return f;
    }
    return NULL;
}

static TmpFile* tmpfs_create_file(TmpFS* t, const char* name) {
    TmpFile* f = (TmpFile*)kmalloc(sizeof(TmpFile));
    if (!f) return NULL;
    size_t nlen = strlen(name);
    f->name = (char*)kmalloc(nlen + 1);
    if (!f->name) { kfree(f); return NULL; }
    memcpy(f->name, name, nlen + 1);
    f->data = NULL;
    f->size = 0;
    f->capacity = 0;
    f->next = t->files;
    t->files = f;
    return f;
}

static int64_t tmpfs_read(File* f, void* buf, uint64_t count) {
    TmpFileHandle* h = (TmpFileHandle*)f->private;
    if (!h) return -1;
    uint64_t remaining = h->file->size - h->pos;
    if (count > remaining) count = remaining;
    for (uint64_t i = 0; i < count; i++) ((uint8_t*)buf)[i] = h->file->data[h->pos + i];
    h->pos += count;
    return (int64_t)count;
}

static int64_t tmpfs_write(File* f, const void* buf, uint64_t count) {
    TmpFileHandle* h = (TmpFileHandle*)f->private;
    if (!h) return -1;

    uint64_t need = h->pos + count;
    if (need > h->file->capacity) {
        uint64_t newcap = h->file->capacity ? h->file->capacity * 2 : 256;
        while (newcap < need) newcap *= 2;
        uint8_t* nd = (uint8_t*)kmalloc(newcap);
        if (!nd) return -1;
        for (uint64_t i = 0; i < h->file->size; i++) nd[i] = h->file->data[i];
        if (h->file->data) kfree(h->file->data);
        h->file->data = nd;
        h->file->capacity = newcap;
    }

    for (uint64_t i = 0; i < count; i++) h->file->data[h->pos + i] = ((const uint8_t*)buf)[i];
    h->pos += count;
    if (h->pos > h->file->size) h->file->size = h->pos;
    return (int64_t)count;
}

static int64_t tmpfs_seek(File* f, int64_t offset, int whence) {
    TmpFileHandle* h = (TmpFileHandle*)f->private;
    if (!h) return -1;
    int64_t base = 0;
    if (whence == SEEK_CUR) base = h->pos;
    else if (whence == SEEK_END) base = h->file->size;
    int64_t newpos = base + offset;
    if (newpos < 0) newpos = 0;
    if ((uint64_t)newpos > h->file->size) newpos = h->file->size;
    h->pos = newpos;
    return newpos;
}

static int tmpfs_file_close(File* f) {
    TmpFileHandle* h = (TmpFileHandle*)f->private;
    if (h) kfree(h);
    kfree(f);
    return 0;
}

static int tmpfs_open(FileSystem* fs, const char* rel_path, uint32_t flags, File** out) {
    TmpFS* t = (TmpFS*)fs->private;
    TmpFile* tf = tmpfs_find_file(t, rel_path);
    if (!tf) {
        if (!(flags & FS_CREATE)) return -1;
        tf = tmpfs_create_file(t, rel_path);
        if (!tf) return -1;
    }

    File* f = (File*)kmalloc(sizeof(File));
    if (!f) return -1;
    TmpFileHandle* h = (TmpFileHandle*)kmalloc(sizeof(TmpFileHandle));
    if (!h) { kfree(f); return -1; }
    h->file = tf;
    h->pos = 0;

    f->fs = fs;
    f->path = rel_path;
    f->size = tf->size;
    f->flags = flags;
    f->private = h;
    f->read = tmpfs_read;
    f->write = tmpfs_write;
    f->seek = tmpfs_seek;
    f->close = tmpfs_file_close;

    *out = f;
    return 0;
}

static FSDriver g_tmpfs_driver = {
    .name = "tmpfs",
    .mount = tmpfs_mount,
    .umount = tmpfs_umount,
    .open = tmpfs_open,
    .next = NULL,
};

void tmpfs_init(void) {
    fs_register_driver(&g_tmpfs_driver);
    serial_emergency_print("tmpfs: registered\n");
}