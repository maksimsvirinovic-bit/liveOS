#include "zip.h"

#define ZIP_LOCAL_SIGNATURE 0x04034B50u
#define ZIP_CENTRAL_SIGNATURE 0x02014B50u
#define ZIP_EOCD_SIGNATURE 0x06054B50u
#define ZIP_METHOD_STORE 0
#define ZIP_METHOD_DEFLATE 8
#define ZIP_FLAG_DATA_DESCRIPTOR 0x0008u
#define ZIP_FLAG_ENCRYPTED 0x0001u

static uint16_t read16(const uint8_t* bytes) {
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int range_valid(uint32_t offset, uint32_t length, uint32_t total) {
    return offset <= total && length <= total - offset;
}

uint32_t zip_crc32(const void* data, uint32_t size) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < size; i++) {
        crc ^= bytes[i];
        for (uint32_t bit = 0; bit < 8; bit++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

typedef struct {
    const uint8_t* input;
    uint32_t input_size;
    uint32_t bit_position;
    uint8_t* output;
    uint32_t output_capacity;
    uint32_t output_size;
} InflateState;

typedef struct {
    uint16_t count[16];
    uint16_t symbols[288];
} HuffmanTable;

static int inflate_bits(InflateState* state, uint32_t count, uint32_t* value) {
    uint64_t total_bits = (uint64_t)state->input_size * 8;
    if (count > 16 || (uint64_t)state->bit_position > total_bits ||
        count > total_bits - state->bit_position) return -1;
    uint32_t result = 0;
    for (uint32_t bit = 0; bit < count; bit++) {
        uint32_t position = state->bit_position++;
        result |= ((state->input[position >> 3] >> (position & 7)) & 1u) << bit;
    }
    *value = result;
    return 0;
}

static int huffman_build(HuffmanTable* table, const uint8_t* lengths, uint32_t symbol_count) {
    uint16_t offsets[16];
    for (uint32_t i = 0; i < 16; i++) table->count[i] = 0;
    for (uint32_t symbol = 0; symbol < symbol_count; symbol++) {
        if (lengths[symbol] > 15) return -1;
        table->count[lengths[symbol]]++;
    }
    if (table->count[0] == symbol_count) return -1;

    int32_t left = 1;
    for (uint32_t length = 1; length <= 15; length++) {
        left <<= 1;
        left -= table->count[length];
        if (left < 0) return -1;
    }
    offsets[1] = 0;
    for (uint32_t length = 1; length < 15; length++) {
        offsets[length + 1] = offsets[length] + table->count[length];
    }
    for (uint32_t symbol = 0; symbol < symbol_count; symbol++) {
        uint8_t length = lengths[symbol];
        if (length) table->symbols[offsets[length]++] = (uint16_t)symbol;
    }
    return 0;
}

static int huffman_decode(InflateState* state, const HuffmanTable* table, uint32_t* symbol) {
    uint32_t code = 0;
    uint32_t first = 0;
    uint32_t index = 0;
    for (uint32_t length = 1; length <= 15; length++) {
        uint32_t bit;
        if (inflate_bits(state, 1, &bit) != 0) return -1;
        code |= bit;
        uint32_t count = table->count[length];
        if (code < first + count) {
            *symbol = table->symbols[index + code - first];
            return 0;
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

static int inflate_emit(InflateState* state, uint8_t value) {
    if (state->output_size >= state->output_capacity) return -1;
    state->output[state->output_size++] = value;
    return 0;
}

static int inflate_codes(InflateState* state, const HuffmanTable* literals,
                         const HuffmanTable* distances) {
    static const uint16_t length_base[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
        31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    static const uint8_t length_extra[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
        2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
    };
    static const uint16_t distance_base[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
        193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
        6145, 8193, 12289, 16385, 24577
    };
    static const uint8_t distance_extra[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
        6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
    };

    for (;;) {
        uint32_t symbol;
        if (huffman_decode(state, literals, &symbol) != 0) return -1;
        if (symbol < 256) {
            if (inflate_emit(state, (uint8_t)symbol) != 0) return -1;
            continue;
        }
        if (symbol == 256) return 0;
        if (symbol < 257 || symbol > 285) return -1;
        uint32_t length_index = symbol - 257;
        uint32_t extra = 0;
        if (inflate_bits(state, length_extra[length_index], &extra) != 0) return -1;
        uint32_t length = length_base[length_index] + extra;
        uint32_t distance_symbol;
        if (huffman_decode(state, distances, &distance_symbol) != 0 || distance_symbol >= 30) return -1;
        if (inflate_bits(state, distance_extra[distance_symbol], &extra) != 0) return -1;
        uint32_t distance = distance_base[distance_symbol] + extra;
        if (!distance || distance > state->output_size ||
            length > state->output_capacity - state->output_size) return -1;
        for (uint32_t i = 0; i < length; i++) {
            state->output[state->output_size] = state->output[state->output_size - distance];
            state->output_size++;
        }
    }
}

static int inflate_dynamic(InflateState* state, HuffmanTable* literals,
                           HuffmanTable* distances) {
    static const uint8_t code_order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    uint32_t value;
    if (inflate_bits(state, 5, &value) != 0) return -1;
    uint32_t literal_count = value + 257;
    if (inflate_bits(state, 5, &value) != 0) return -1;
    uint32_t distance_count = value + 1;
    if (inflate_bits(state, 4, &value) != 0) return -1;
    uint32_t code_count = value + 4;
    uint8_t code_lengths[19] = {0};
    for (uint32_t i = 0; i < code_count; i++) {
        if (inflate_bits(state, 3, &value) != 0) return -1;
        code_lengths[code_order[i]] = (uint8_t)value;
    }

    HuffmanTable code_table;
    if (huffman_build(&code_table, code_lengths, 19) != 0) return -1;
    uint8_t lengths[320] = {0};
    uint32_t total = literal_count + distance_count;
    for (uint32_t i = 0; i < total;) {
        uint32_t symbol;
        if (huffman_decode(state, &code_table, &symbol) != 0) return -1;
        if (symbol < 16) {
            lengths[i++] = (uint8_t)symbol;
            continue;
        }
        uint32_t repeat, repeated_value;
        if (symbol == 16) {
            if (!i || inflate_bits(state, 2, &value) != 0) return -1;
            repeat = value + 3;
            repeated_value = lengths[i - 1];
        } else if (symbol == 17) {
            if (inflate_bits(state, 3, &value) != 0) return -1;
            repeat = value + 3;
            repeated_value = 0;
        } else if (symbol == 18) {
            if (inflate_bits(state, 7, &value) != 0) return -1;
            repeat = value + 11;
            repeated_value = 0;
        } else {
            return -1;
        }
        if (repeat > total - i) return -1;
        while (repeat--) lengths[i++] = (uint8_t)repeated_value;
    }
    if (lengths[256] == 0) return -1;
    return huffman_build(literals, lengths, literal_count) == 0 &&
           huffman_build(distances, lengths + literal_count, distance_count) == 0 ? 0 : -1;
}

int zip_inflate_raw(const void* input, uint32_t input_size, void* output,
                    uint32_t output_capacity, uint32_t* output_size) {
    if (!input || (!output && output_capacity) || !output_size) return -1;
    InflateState state = {
        .input = (const uint8_t*)input,
        .input_size = input_size,
        .bit_position = 0,
        .output = (uint8_t*)output,
        .output_capacity = output_capacity,
        .output_size = 0
    };
    uint32_t final_block = 0;
    while (!final_block) {
        uint32_t block_type;
        if (inflate_bits(&state, 1, &final_block) != 0 ||
            inflate_bits(&state, 2, &block_type) != 0) return -1;
        if (block_type == 0) {
            state.bit_position = (state.bit_position + 7) & ~7u;
            uint32_t length, complement;
            if (inflate_bits(&state, 16, &length) != 0 ||
                inflate_bits(&state, 16, &complement) != 0 ||
                ((length ^ 0xFFFFu) & 0xFFFFu) != complement ||
                length > state.output_capacity - state.output_size) return -1;
            for (uint32_t i = 0; i < length; i++) {
                uint32_t byte;
                if (inflate_bits(&state, 8, &byte) != 0 || inflate_emit(&state, (uint8_t)byte) != 0) return -1;
            }
        } else if (block_type == 1 || block_type == 2) {
            uint8_t literal_lengths[288] = {0};
            uint8_t distance_lengths[32] = {0};
            HuffmanTable literals, distances;
            if (block_type == 1) {
                for (uint32_t i = 0; i <= 143; i++) literal_lengths[i] = 8;
                for (uint32_t i = 144; i <= 255; i++) literal_lengths[i] = 9;
                for (uint32_t i = 256; i <= 279; i++) literal_lengths[i] = 7;
                for (uint32_t i = 280; i <= 287; i++) literal_lengths[i] = 8;
                for (uint32_t i = 0; i < 32; i++) distance_lengths[i] = 5;
                if (huffman_build(&literals, literal_lengths, 288) != 0 ||
                    huffman_build(&distances, distance_lengths, 32) != 0) return -1;
            } else if (inflate_dynamic(&state, &literals, &distances) != 0) {
                return -1;
            }
            if (inflate_codes(&state, &literals, &distances) != 0) return -1;
        } else {
            return -1;
        }
    }
    *output_size = state.output_size;
    return 0;
}

static int path_char_valid(char ch) {
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9')) return 1;
    return ch == '_' || ch == '-' || ch == '.';
}

int zip_path_is_safe_83(const char* path, int is_directory) {
    if (!path || !path[0] || path[0] == '/' || path[0] == '\\') return 0;
    uint32_t path_length = 0;
    while (path[path_length]) path_length++;
    uint32_t effective_length = path_length;
    if (is_directory) {
        if (path_length < 2 || path[path_length - 1] != '/') return 0;
        effective_length--;
    } else if (path[path_length - 1] == '/') {
        return 0;
    }
    uint32_t component_length = 0;
    uint32_t base_length = 0;
    uint32_t extension_length = 0;
    int extension_seen = 0;
    for (uint32_t i = 0; i <= effective_length; i++) {
        char ch = i == effective_length ? '\0' : path[i];
        if (ch == '\0' || ch == '/') {
            if (ch == '/' && i + 1 < effective_length && path[i + 1] == '/') return 0;
            if (component_length == 0 || (component_length == 1 && path[i - 1] == '.') ||
                (component_length == 2 && path[i - 2] == '.' && path[i - 1] == '.')) return 0;
            if (base_length == 0 || base_length > 8 || extension_length > 3) return 0;
            if (ch == '\0') return 1;
            component_length = 0;
            base_length = 0;
            extension_length = 0;
            extension_seen = 0;
            continue;
        }
        if (!path_char_valid(ch)) return 0;
        component_length++;
        if (ch == '.') {
            if (extension_seen || base_length == 0) return 0;
            extension_seen = 1;
        } else if (extension_seen) {
            extension_length++;
        } else {
            base_length++;
        }
        if (component_length > 12) return 0;
    }
    return 0;
}

static int copy_path(char* output, uint32_t capacity, const uint8_t* input,
                     uint16_t length, int* is_directory) {
    if (length == 0 || length >= capacity) return -1;
    uint32_t out = 0;
    *is_directory = input[length - 1] == '/';
    for (uint32_t i = 0; i < length; i++) {
        uint8_t ch = input[i];
        if (ch == 0 || ch == '\\' || ch < 0x20 || ch >= 0x7F) return -1;
        output[out++] = (char)ch;
    }
    output[out] = '\0';
    return zip_path_is_safe_83(output, *is_directory) ? 0 : -1;
}

static int paths_collide(const ZipEntry* left_entry, const ZipEntry* right_entry) {
    const char* left = left_entry->path;
    const char* right = right_entry->path;
    uint32_t left_length = 0;
    uint32_t right_length = 0;
    while (left[left_length]) left_length++;
    while (right[right_length]) right_length++;
    if (left_entry->is_directory && left_length) left_length--;
    if (right_entry->is_directory && right_length) right_length--;

    uint32_t i = 0;
    while (i < left_length && i < right_length) {
        char a = left[i];
        char b = right[i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return 0;
        i++;
    }
    if (left_length == right_length) return 1;
    if (left_length < right_length) {
        return !left_entry->is_directory && right[i] == '/';
    }
    return !right_entry->is_directory && left[i] == '/';
}

int zip_open(ZipArchive* archive, const void* bytes, uint32_t size) {
    if (!archive || !bytes || size < 22) return -1;
    const uint8_t* data = (const uint8_t*)bytes;
    uint32_t search_start = size > 65557u ? size - 65557u : 0;
    uint32_t eocd = 0xFFFFFFFFu;
    for (uint32_t cursor = size - 22;; cursor--) {
        if (read32(data + cursor) == ZIP_EOCD_SIGNATURE) {
            uint16_t comment_length = read16(data + cursor + 20);
            if (range_valid(cursor, 22u + comment_length, size) &&
                cursor + 22u + comment_length == size) {
                eocd = cursor;
                break;
            }
        }
        if (cursor == search_start) break;
    }
    if (eocd == 0xFFFFFFFFu) return -1;

    uint16_t disk_number = read16(data + eocd + 4);
    uint16_t central_disk = read16(data + eocd + 6);
    uint16_t disk_entries = read16(data + eocd + 8);
    uint16_t entry_count = read16(data + eocd + 10);
    uint32_t central_size = read32(data + eocd + 12);
    uint32_t central_offset = read32(data + eocd + 16);
    if (disk_number || central_disk || disk_entries != entry_count ||
        entry_count > ZIP_MAX_ENTRIES || central_offset > eocd ||
        !range_valid(central_offset, central_size, eocd)) return -1;

    archive->bytes = data;
    archive->size = size;
    archive->entry_count = 0;
    archive->total_uncompressed_size = 0;
    uint32_t cursor = central_offset;
    uint32_t central_end = central_offset + central_size;

    for (uint32_t i = 0; i < entry_count; i++) {
        if (!range_valid(cursor, 46, central_end) || read32(data + cursor) != ZIP_CENTRAL_SIGNATURE) return -1;
        uint16_t flags = read16(data + cursor + 8);
        uint16_t method = read16(data + cursor + 10);
        uint32_t crc = read32(data + cursor + 16);
        uint32_t compressed_size = read32(data + cursor + 20);
        uint32_t uncompressed_size = read32(data + cursor + 24);
        uint16_t name_length = read16(data + cursor + 28);
        uint16_t extra_length = read16(data + cursor + 30);
        uint16_t comment_length = read16(data + cursor + 32);
        uint16_t start_disk = read16(data + cursor + 34);
        uint32_t local_offset = read32(data + cursor + 42);
        uint32_t record_length = 46u + name_length + extra_length + comment_length;
        if (compressed_size > ZIP_MAX_ENTRY_SIZE) return -1;
        if (!range_valid(cursor, record_length, central_end) || start_disk != 0 ||
            (flags & (ZIP_FLAG_ENCRYPTED | ZIP_FLAG_DATA_DESCRIPTOR)) ||
            (method != ZIP_METHOD_STORE && method != ZIP_METHOD_DEFLATE) ||
            (method == ZIP_METHOD_STORE && compressed_size != uncompressed_size) ||
            compressed_size == 0xFFFFFFFFu || uncompressed_size == 0xFFFFFFFFu ||
            uncompressed_size > ZIP_MAX_ENTRY_SIZE ||
            archive->total_uncompressed_size > ZIP_MAX_TOTAL_SIZE - uncompressed_size ||
            !range_valid(local_offset, 30, size) || read32(data + local_offset) != ZIP_LOCAL_SIGNATURE) return -1;

        ZipEntry* entry = &archive->entries[archive->entry_count];
        int is_directory = 0;
        if (copy_path(entry->path, sizeof(entry->path), data + cursor + 46,
                      name_length, &is_directory) != 0) return -1;
        entry->is_directory = (uint8_t)is_directory;
        for (uint32_t previous = 0; previous < archive->entry_count; previous++) {
            if (paths_collide(&archive->entries[previous], entry)) return -1;
        }
        uint16_t local_name_length = read16(data + local_offset + 26);
        uint16_t local_extra_length = read16(data + local_offset + 28);
        uint32_t local_data_offset = local_offset + 30u + local_name_length + local_extra_length;
        if (!range_valid(local_data_offset, compressed_size, size) ||
            local_name_length != name_length || read16(data + local_offset + 6) != flags ||
            read16(data + local_offset + 8) != method ||
            read32(data + local_offset + 14) != crc ||
            read32(data + local_offset + 18) != compressed_size ||
            read32(data + local_offset + 22) != uncompressed_size) return -1;
        for (uint32_t n = 0; n < name_length; n++) {
            if (data[local_offset + 30 + n] != data[cursor + 46 + n]) return -1;
        }

        entry->data_offset = local_data_offset;
        entry->compressed_size = compressed_size;
        entry->uncompressed_size = uncompressed_size;
        entry->crc32 = crc;
        entry->method = method;
        entry->is_directory = (uint8_t)is_directory;
        archive->entry_count++;
        archive->total_uncompressed_size += uncompressed_size;
        cursor += record_length;
    }
    return cursor == central_end ? 0 : -1;
}

int zip_extract(const ZipArchive* archive, uint32_t index, void* output,
                uint32_t capacity, uint32_t* output_size) {
    if (!archive || index >= archive->entry_count || !output_size) return -1;
    const ZipEntry* entry = &archive->entries[index];
    if (entry->is_directory ||
        (entry->method != ZIP_METHOD_STORE && entry->method != ZIP_METHOD_DEFLATE) ||
        entry->uncompressed_size > capacity || (!output && entry->uncompressed_size)) return -1;
    const uint8_t* source = archive->bytes + entry->data_offset;
    if (entry->method == ZIP_METHOD_STORE) {
        uint8_t* destination = (uint8_t*)output;
        for (uint32_t i = 0; i < entry->uncompressed_size; i++) destination[i] = source[i];
        *output_size = entry->uncompressed_size;
    } else if (zip_inflate_raw(source, entry->compressed_size, output, capacity, output_size) != 0 ||
               *output_size != entry->uncompressed_size) {
        return -1;
    }
    if (zip_crc32(output, *output_size) != entry->crc32) return -1;
    return 0;
}