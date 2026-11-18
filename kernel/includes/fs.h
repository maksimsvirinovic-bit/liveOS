#ifndef FS_H
#define FS_H

#include "x86_64/syscall.h"
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "block.h"

typedef struct FileSystem FileSystem;
typedef struct File File;
typedef struct FSDriver FSDriver;

typedef struct {
    char     name[64];
    uint8_t  attr;
    uint8_t  _pad[3];
    uint32_t size;
} DirEntry;

#define FS_ATTR_DIR      0x10
#define FS_ATTR_FILE     0x20
#define FS_ATTR_READONLY 0x01
#define FS_ATTR_HIDDEN   0x02
#define FS_ATTR_SYSTEM   0x04

struct FSDriver {
    const char* name;
    int  (*mount)  (BlockDevice* dev, FileSystem** out);
    void (*umount) (FileSystem* fs);
    int  (*open)   (FileSystem* fs, const char* rel_path,
                    uint32_t flags, File** out);
    int  (*mkdir)  (FileSystem* fs, const char* rel_path);
    int  (*unlink) (FileSystem* fs, const char* rel_path);
    int  (*rmdir)  (FileSystem* fs, const char* rel_path);
    int  (*rmdir_r)(FileSystem* fs, const char* rel_path);
    int  (*readdir)(FileSystem* fs, File* dir, uint32_t index, DirEntry* out);

    FSDriver* next;
};

struct FileSystem {
    const char*  mountpoint;
    FSDriver*    driver;
    BlockDevice* dev;
    void*        private;
    uint32_t     flags;
    uint32_t     cwd_cluster;
    char         cwd_path[256];
    FileSystem*  next;
};

#define FS_READ      0x01
#define FS_WRITE     0x02
#define FS_CREATE    0x04
#define FS_APPEND    0x08
#define FS_DIRECTORY 0x10
#define FS_TRUNCATE  0x20

struct File {
    FileSystem* fs;
    char*        path;
    uint64_t    size;
    uint32_t    flags;
    void*       private;

    int64_t (*read)   (File* f, void* buf, uint64_t count);
    int64_t (*write)  (File* f, const void* buf, uint64_t count);
    int64_t (*seek)   (File* f, int64_t offset, int whence);
    int     (*close)  (File* f);
};

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

void        fs_init(void);
int         fs_register_driver(FSDriver* drv);
FSDriver*   fs_find_driver(const char* name);

int         fs_mount(BlockDevice* dev, const char* driver_name,
                     const char* mountpoint, FileSystem** out);
int         fs_umount(FileSystem* fs);
FileSystem* fs_find_by_path(const char* path);

int         fs_open(const char* path, uint32_t flags, File** out);
int         fs_close(File* f);
int64_t     fs_read(File* f, void* buf, uint64_t count);
int64_t     fs_write(File* f, const void* buf, uint64_t count);
int64_t     fs_seek(File* f, int64_t offset, int whence);

int         fs_mkdir(const char* path);
int         fs_unlink(const char* path);
int         fs_rmdir(const char* path);
int         fs_rmdir_r(const char* path);


int         fs_opendir(const char* path, File** out);
int         fs_readdir(File* dir, uint32_t index, DirEntry* out);
int         fs_closedir(File* dir);

#ifdef __cplusplus
}
#endif

#endif