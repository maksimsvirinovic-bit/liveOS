#include "fs.h"
#include "heap.h"
#include <stddef.h>
#include <str.h>

static FSDriver*   g_fs_drivers = NULL;
static FileSystem* g_mounts = NULL;

void fs_init(void) {
    g_fs_drivers = NULL;
    g_mounts = NULL;
}

int fs_register_driver(FSDriver* drv) {
    if (!drv || !drv->name || !drv->mount) return -1;
    drv->next = g_fs_drivers;
    g_fs_drivers = drv;
    return 0;
}

int fs_unregister_driver(FSDriver* drv) {
    FSDriver** pp = &g_fs_drivers;
    while (*pp) {
        if (*pp == drv) { *pp = drv->next; return 0; }
        pp = &(*pp)->next;
    }
    return -1;
}

FSDriver* fs_find_driver(const char* name) {
    for (FSDriver* d = g_fs_drivers; d; d = d->next) {
        if (strcmp(d->name, name) == 0) return d;
    }
    return NULL;
}

int fs_mount(BlockDevice* dev, const char* driver_name,
             const char* mountpoint, FileSystem** out) {
    FSDriver* drv = fs_find_driver(driver_name);
    if (!drv) return -1;

    FileSystem* fs = (FileSystem*)kmalloc(sizeof(FileSystem));
    if (!fs) return -1;

    fs->dev = dev;
    fs->driver = drv;
    fs->mountpoint = mountpoint;
    fs->private = NULL;
    fs->flags = 0;
    fs->next = NULL;

    if (drv->mount(dev, &fs) != 0) {
        kfree(fs);
        return -1;
    }

    fs->next = g_mounts;
    g_mounts = fs;
    *out = fs;
    return 0;
}

int fs_umount(FileSystem* fs) {
    FileSystem** pp = &g_mounts;
    while (*pp) {
        if (*pp == fs) {
            *pp = fs->next;
            if (fs->driver->umount) fs->driver->umount(fs);
            kfree(fs);
            return 0;
        }
        pp = &(*pp)->next;
    }
    return -1;
}

FileSystem* fs_find_by_path(const char* path) {
    // TODO: исправить проблему с выдачей файловой системы
    // которая вешает regfs.
    FileSystem* best = NULL;
    size_t best_len = 0;
    for (FileSystem* fs = g_mounts; fs; fs = fs->next) {
        size_t len = strlen(fs->mountpoint);
        if (strncmp(path, fs->mountpoint, len) == 0 && len > best_len) {
            best = fs;
            best_len = len;
        }
    }
    return best;
}

int fs_open(const char* path, uint32_t flags, File** out) {
    FileSystem* fs = fs_find_by_path(path);
    if (!fs || !fs->driver->open) return -1;
    const char* rel = path + strlen(fs->mountpoint);
    if (strcmp(fs->mountpoint, "/") == 0) {
        rel = path;
    } else if (*rel == '/') {
        rel++;
    }
    return fs->driver->open(fs, rel, flags, out);
}

int fs_close(File* f) {
    if (!f || !f->close) return -1;
    return f->close(f);
}
int64_t fs_read(File* f, void* buf, uint64_t count) {
    if (!f || !f->read) return -1;
    return f->read(f, buf, count);
}
int64_t fs_write(File* f, const void* buf, uint64_t count) {
    if (!f || !f->write) return -1;
    return f->write(f, buf, count);
}
int64_t fs_seek(File* f, int64_t offset, int whence) {
    if (!f || !f->seek) return -1;
    return f->seek(f, offset, whence);
}

int64_t fs_read_file(const char* path, void* buf, uint64_t max_size) {
    File* f;
    if (fs_open(path, FS_READ, &f) != 0) return -1;
    int64_t n = fs_read(f, buf, max_size);
    fs_close(f);
    return n;
}

static const char* strip_mountpoint(FileSystem* fs, const char* path) {
    if (strcmp(fs->mountpoint, "/") == 0) return path;
    const char* rel = path + strlen(fs->mountpoint);
    if (*rel == '/') rel++;
    if (*rel == '\0') rel = ".";
    return rel;
}

int fs_unlink(const char* path) {
    FileSystem* fs = fs_find_by_path(path);
    if (!fs || !fs->driver->unlink) return -1;
    return fs->driver->unlink(fs, strip_mountpoint(fs, path));
}

int fs_rmdir(const char* path) {
    FileSystem* fs = fs_find_by_path(path);
    if (!fs || !fs->driver->rmdir) return -1;
    return fs->driver->rmdir(fs, strip_mountpoint(fs, path));
}

int fs_rmdir_r(const char* path) {
    FileSystem* fs = fs_find_by_path(path);
    if (!fs) return -1;
    if (fs->driver->rmdir_r)
        return fs->driver->rmdir_r(fs, strip_mountpoint(fs, path));
    if (fs->driver->rmdir)
        return fs->driver->rmdir(fs, strip_mountpoint(fs, path));
    return -1;
}

int fs_opendir(const char* path, File** out) {
    FileSystem* fs = fs_find_by_path(path);
    if (!fs || !fs->driver->open) return -1;
    return fs->driver->open(fs, strip_mountpoint(fs, path),
                            FS_READ | FS_DIRECTORY, out);
}

int fs_readdir(File* dir, uint32_t index, DirEntry* out) {
    if (!dir || !dir->fs || !dir->fs->driver->readdir) return -1;
    return dir->fs->driver->readdir(dir->fs, dir, index, out);
}

int fs_closedir(File* dir) {
    if (!dir) return -1;
    return fs_close(dir);
}

int fs_mkdir(const char* path) {
    FileSystem* fs = fs_find_by_path(path);
    if (!fs || !fs->driver->mkdir) return -1;
    const char* rel = strip_mountpoint(fs, path);
    return fs->driver->mkdir(fs, rel);
}