#include "../user/zip.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint8_t archive_bytes[256];
static uint32_t archive_size;
static uint32_t test_compressed_size;

static void put16(uint8_t* out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t* out, uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static void build_archive(const char* name, const uint8_t* payload,
                          uint16_t method, uint32_t uncompressed_size, uint32_t crc) {
    uint16_t name_length = 0;
    while (name[name_length]) name_length++;
    uint32_t cursor = 0;

    put32(archive_bytes + cursor, 0x04034B50); cursor += 4;
    put16(archive_bytes + cursor, 20); cursor += 2;
    put16(archive_bytes + cursor, 0); cursor += 2;
    put16(archive_bytes + cursor, method); cursor += 2;
    put16(archive_bytes + cursor, 0); cursor += 2;
    put16(archive_bytes + cursor, 0); cursor += 2;
    put32(archive_bytes + cursor, crc); cursor += 4;
    put32(archive_bytes + cursor, test_compressed_size); cursor += 4;
    put32(archive_bytes + cursor, uncompressed_size); cursor += 4;
    put16(archive_bytes + cursor, name_length); cursor += 2;
    put16(archive_bytes + cursor, 0); cursor += 2;
    for (uint32_t i = 0; i < name_length; i++) archive_bytes[cursor++] = (uint8_t)name[i];
    for (uint32_t i = 0; i < test_compressed_size; i++) archive_bytes[cursor++] = payload[i];

    uint32_t central_offset = cursor;
    put32(archive_bytes + cursor, 0x02014B50); cursor += 4;
    put16(archive_bytes + cursor, 20); cursor += 2;
    put16(archive_bytes + cursor, 20); cursor += 2;
    put16(archive_bytes + cursor, 0); cursor += 2;
    put16(archive_bytes + cursor, method); cursor += 2;
    put16(archive_bytes + cursor, 0); cursor += 2;
    put16(archive_bytes + cursor, 0); cursor += 2;
    put32(archive_bytes + cursor, crc); cursor += 4;
    put32(archive_bytes + cursor, test_compressed_size); cursor += 4;
    put32(archive_bytes + cursor, uncompressed_size); cursor += 4;
    put16(archive_bytes + cursor, name_length); cursor += 2;
    put16(archive_bytes + cursor, 0); cursor += 2;
    put16(archive_bytes + cursor, 0); cursor += 2;
    put16(archive_bytes + cursor, 0); cursor += 2;
    put16(archive_bytes + cursor, 0); cursor += 2;
    put32(archive_bytes + cursor, 0); cursor += 4;
    put32(archive_bytes + cursor, 0); cursor += 4;
    for (uint32_t i = 0; i < name_length; i++) archive_bytes[cursor++] = (uint8_t)name[i];

    uint32_t central_size = cursor - central_offset;
    put32(archive_bytes + cursor, 0x06054B50); cursor += 4;
    put16(archive_bytes + cursor, 0); cursor += 2;
    put16(archive_bytes + cursor, 0); cursor += 2;
    put16(archive_bytes + cursor, 1); cursor += 2;
    put16(archive_bytes + cursor, 1); cursor += 2;
    put32(archive_bytes + cursor, central_size); cursor += 4;
    put32(archive_bytes + cursor, central_offset); cursor += 4;
    put16(archive_bytes + cursor, 0); cursor += 2;
    archive_size = cursor;
}

static void test_crc(void) {
    static const char text[] = "123456789";
    assert(zip_crc32(text, 9) == 0xCBF43926u);
}

static void test_deflate(void) {
    static const uint8_t compressed[] = {
        0x2B, 0x48, 0x4C, 0xCE, 0x4E, 0x4C, 0x4F, 0x55,
        0x48, 0x49, 0x4D, 0xCB, 0x49, 0x2C, 0x49, 0x55,
        0x28, 0xCE, 0xCD, 0xCF, 0x4E, 0x55, 0x28, 0x49,
        0x2D, 0x2E, 0xE1, 0x2A, 0xA0, 0xB6, 0x14, 0x00
    };
    static const char expected[] =
        "package deflate smoke test\npackage deflate smoke test\n"
        "package deflate smoke test\npackage deflate smoke test\n";
    uint8_t output[128];
    uint32_t output_size = 0;
    assert(zip_inflate_raw(compressed, sizeof(compressed), output, sizeof(output), &output_size) == 0);
    assert(output_size == sizeof(expected) - 1);
    for (uint32_t i = 0; i < output_size; i++) assert(output[i] == (uint8_t)expected[i]);

    test_compressed_size = sizeof(compressed);
    build_archive("TEXT.TXT", compressed, 8,
                  sizeof(expected) - 1, zip_crc32(expected, sizeof(expected) - 1));
    ZipArchive archive;
    assert(zip_open(&archive, archive_bytes, archive_size) == 0);
    output_size = 0;
    assert(zip_extract(&archive, 0, output, sizeof(output), &output_size) == 0);
    assert(output_size == sizeof(expected) - 1);
}

static void test_extract(void) {
    static const uint8_t payload[] = {'p', 'k', 'g'};
    test_compressed_size = sizeof(payload);
    build_archive("APP.ELF", payload, 0, sizeof(payload), zip_crc32(payload, sizeof(payload)));
    ZipArchive archive;
    assert(zip_open(&archive, archive_bytes, archive_size) == 0);
    assert(archive.entry_count == 1);
    assert(archive.entries[0].uncompressed_size == sizeof(payload));
    uint8_t output[8] = {0};
    uint32_t output_size = 0;
    assert(zip_extract(&archive, 0, output, sizeof(output), &output_size) == 0);
    assert(output_size == sizeof(payload));
    assert(output[0] == 'p' && output[2] == 'g');
    archive_bytes[30 + 7] ^= 1;
    assert(zip_extract(&archive, 0, output, sizeof(output), &output_size) != 0);
}

static void test_reject_paths_and_methods(void) {
    static const uint8_t payload[] = {'x'};
    ZipArchive archive;
    assert(!zip_path_is_safe_83("../EVIL.TXT", 0));
    assert(!zip_path_is_safe_83("/ABS.TXT", 0));
    assert(!zip_path_is_safe_83("LONGFILENAME.TXT", 0));
    assert(zip_path_is_safe_83("BIN/APP.ELF", 0));
    assert(zip_path_is_safe_83("BIN/", 1));
    assert(!zip_path_is_safe_83("BIN", 1));

    test_compressed_size = sizeof(payload);
    build_archive("../EVIL.TXT", payload, 0, sizeof(payload), zip_crc32(payload, sizeof(payload)));
    assert(zip_open(&archive, archive_bytes, archive_size) != 0);
    build_archive("APP.ELF", payload, 12, sizeof(payload), zip_crc32(payload, sizeof(payload)));
    assert(zip_open(&archive, archive_bytes, archive_size) != 0);
}

int main(void) {
    test_crc();
    test_deflate();
    test_extract();
    test_reject_paths_and_methods();
    puts("zip tests passed");
    return 0;
}
