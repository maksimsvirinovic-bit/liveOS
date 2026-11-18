#ifndef REGFS_H
#define REGFS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "fs.h"
#include "block.h"
#define HIVE_MAGIC  0x4852764C
#define HIVE_VERSION 1

#define HIVE_KEY_NAME_MAX   32
#define HIVE_VAL_NAME_MAX   32
#define REG_TYPE_UINT32  0
#define REG_TYPE_UINT64  1
#define REG_TYPE_STRING  2
#define REG_TYPE_BINARY  3
#define REG_TYPE_HEX     4

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t key_count;
    uint32_t val_count;
    uint32_t data_size;
    uint8_t  reserved[12];
} __attribute__((packed)) HiveHeader;

typedef struct {
    char     name[HIVE_KEY_NAME_MAX];
    int32_t  parent_idx;
    uint32_t first_val_idx;
    uint32_t val_count;
    uint32_t flags;
} __attribute__((packed)) HiveKey;

typedef struct {
    char     name[HIVE_VAL_NAME_MAX];
    uint8_t  type;
    uint8_t  flags;
    uint32_t data_offset;
    uint32_t data_size;
} __attribute__((packed)) HiveValue;

_Static_assert(sizeof(HiveHeader) == 32, "HiveHeader must be 32 bytes");
_Static_assert(sizeof(HiveKey) == 48, "HiveKey must be 48 bytes");
_Static_assert(sizeof(HiveValue) == 42, "HiveValue must be 42 bytes");
typedef struct RegNode {
    char     name[HIVE_KEY_NAME_MAX];
    uint32_t flags;
    struct RegNode* parent;
    struct RegNode* first_child;
    struct RegNode* next_sibling;
    struct RegValue* first_value;
    struct RegValue* next_value_owner;
} RegNode;

typedef struct RegValue {
    char     name[HIVE_VAL_NAME_MAX];
    uint8_t  type;
    uint8_t  flags;
    uint32_t data_size;
    void*    data;
    struct RegValue* next;
} RegValue;
typedef struct RegFS {
    BlockDevice* dev;
    const char*  hive_path;
    RegNode*     root;
    char*        text_buf;
    uint32_t     text_size;
    uint32_t     text_pos;
    int          dirty;
} RegFS;

typedef struct {
    char name[HIVE_VAL_NAME_MAX];
    char text[64];
    uint32_t type;
    uint32_t is_key;
} RegEntryInfo;

void regfs_init(void);
int  regfs_mount(BlockDevice* dev, FileSystem** out);
void regfs_umount(FileSystem* fs);
void regfs_set_hive_path(const char* path);
int  regfs_load_hive(RegFS* rfs, const uint8_t* data, uint32_t size);
int  regfs_save_hive(RegFS* rfs, uint8_t** out_data, uint32_t* out_size);
int  regfs_decompile(RegFS* rfs, char** out_text, uint32_t* out_size);
int  regfs_compile(RegFS* rfs, const char* text, uint32_t size);
RegNode*  regfs_find_key(RegNode* root, const char* path);
RegValue* regfs_find_value(RegNode* key, const char* name);
int regfs_get_str(RegFS* rfs, const char* path, char* out, uint32_t out_size);
int regfs_set_u32(RegFS* rfs, const char* path, uint32_t value);
int regfs_set_str(RegFS* rfs, const char* path, const char* value);
int regfs_enum(RegFS* rfs, const char* path, uint32_t index, RegEntryInfo* out);
int regfs_sync(RegFS* rfs);
uint64_t regfs_get_u64(RegFS* rfs, const char* path);
uint32_t regfs_get_u32(RegFS* rfs, const char* path);
void regfs_unlock_easter_eggs(RegFS* rfs);

#ifdef __cplusplus
}
#endif

#endif