#ifndef LIVEOS_ZIP_H
#define LIVEOS_ZIP_H

#include <stdint.h>

#define ZIP_MAX_ENTRIES 64
#define ZIP_MAX_ENTRY_SIZE (2u * 1024u * 1024u)
#define ZIP_MAX_TOTAL_SIZE (8u * 1024u * 1024u)
#define ZIP_PATH_MAX 192

typedef struct {
    char path[ZIP_PATH_MAX];
    uint32_t data_offset;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t crc32;
    uint16_t method;
    uint8_t is_directory;
} ZipEntry;

typedef struct {
    const uint8_t* bytes;
    uint32_t size;
    ZipEntry entries[ZIP_MAX_ENTRIES];
    uint32_t entry_count;
    uint32_t total_uncompressed_size;
} ZipArchive;

uint32_t zip_crc32(const void* data, uint32_t size);
int zip_inflate_raw(const void* input, uint32_t input_size, void* output,
                    uint32_t output_capacity, uint32_t* output_size);
int zip_open(ZipArchive* archive, const void* bytes, uint32_t size);
int zip_extract(const ZipArchive* archive, uint32_t index, void* output,
                uint32_t capacity, uint32_t* output_size);
int zip_path_is_safe_83(const char* path, int is_directory);

#endif