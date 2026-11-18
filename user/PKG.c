#include "sys.h"
#include "ubase.h"
#include "zip.h"

#define PACKAGE_ARCHIVE_MAX (1024u * 1024u)
#define PACKAGE_ENTRY_MAX (128u * 1024u)
#define PACKAGE_ROOT "/PKG"

static int file_exists(const char* path) {
    int fd = sys_open(path, FS_READ);
    if (fd < 0) return 0;
    sys_close(fd);
    return 1;
}

static int directory_exists(const char* path) {
    int fd = sys_opendir(path);
    if (fd < 0) return 0;
    sys_close(fd);
    return 1;
}

static int ensure_directory(const char* path) {
    if (sys_mkdir(path) == 0) return 0;
    return directory_exists(path) ? 0 : -1;
}

static int make_directories(const char* path, int include_final) {
    char current[ZIP_PATH_MAX + 8];
    uint32_t output = 0;
    current[output++] = '/';
    current[output++] = 'P';
    current[output++] = 'K';
    current[output++] = 'G';
    current[output++] = '/';
    current[output] = '\0';

    uint32_t index = 0;
    while (path[index]) {
        if (path[index] == '/') {
            current[output] = '\0';
            if (ensure_directory(current) != 0) return -1;
            if (path[index + 1] == '\0') return 0;
        }
        if (output + 1 >= sizeof(current)) return -1;
        current[output++] = path[index++];
    }
    current[output] = '\0';
    if (include_final && ensure_directory(current) != 0) return -1;
    return 0;
}

static int build_destination(const char* relative, char* output, uint32_t capacity) {
    uint32_t pos = 0;
    const char* prefix = PACKAGE_ROOT "/";
    while (*prefix) {
        if (pos + 1 >= capacity) return -1;
        output[pos++] = *prefix++;
    }
    while (*relative) {
        if (pos + 1 >= capacity) return -1;
        output[pos++] = *relative++;
    }
    output[pos] = '\0';
    return 0;
}

static int install_archive(ZipArchive* archive) {
    char destination[ZIP_PATH_MAX + 8];
    uint8_t* output = (uint8_t*)sys_mmap_anon(PACKAGE_ENTRY_MAX);
    if (!output) return -1;

    for (uint32_t i = 0; i < archive->entry_count; i++) {
        ZipEntry* entry = &archive->entries[i];
        if (entry->is_directory) continue;
        uint32_t extracted_size = 0;
        if (entry->uncompressed_size > PACKAGE_ENTRY_MAX ||
            zip_extract(archive, i, output, PACKAGE_ENTRY_MAX, &extracted_size) != 0 ||
            extracted_size != entry->uncompressed_size) {
            print("pkg: CRC or extraction error\n");
            return -1;
        }
        for (uint32_t j = 0; j < i; j++) {
            const char* left = archive->entries[j].path;
            const char* right = entry->path;
            uint32_t k = 0;
            while (left[k] && right[k]) {
                char a = left[k], b = right[k];
                if (a >= 'a' && a <= 'z') a -= 32;
                if (b >= 'a' && b <= 'z') b -= 32;
                if (a != b) break;
                k++;
            }
            if (!left[k] && !right[k]) {
                print("pkg: duplicate archive path\n");
                return -1;
            }
        }
    }

    for (uint32_t i = 0; i < archive->entry_count; i++) {
        ZipEntry* entry = &archive->entries[i];
        if (build_destination(entry->path, destination, sizeof(destination)) != 0) return -1;
        if (entry->is_directory) {
            if (make_directories(entry->path, 1) != 0) return -1;
        } else if (file_exists(destination)) {
            print("pkg: refusing to overwrite ");
            print(destination);
            print("\n");
            return -1;
        }
    }

    if (ensure_directory(PACKAGE_ROOT) != 0) return -1;

    for (uint32_t i = 0; i < archive->entry_count; i++) {
        ZipEntry* entry = &archive->entries[i];
        if (entry->is_directory) continue;
        uint32_t extracted_size = 0;
        if (zip_extract(archive, i, output, PACKAGE_ENTRY_MAX, &extracted_size) != 0 ||
            extracted_size != entry->uncompressed_size) return -1;
        if (build_destination(entry->path, destination, sizeof(destination)) != 0 ||
            make_directories(entry->path, 0) != 0) return -1;

        int fd = sys_open(destination, FS_WRITE | FS_CREATE | FS_TRUNCATE);
        if (fd < 0) {
            print("pkg: cannot create ");
            print(destination);
            print("\n");
            return -1;
        }
        uint32_t written = 0;
        while (written < entry->uncompressed_size) {
            uint32_t chunk = entry->uncompressed_size - written;
            if (chunk > 4096) chunk = 4096;
            int count = sys_write(fd, output + written, chunk);
            if (count <= 0) {
                sys_close(fd);
                return -1;
            }
            written += (uint32_t)count;
        }
        if (sys_close(fd) != 0) return -1;
        print("installed ");
        print(destination);
        print("\n");
    }
    return 0;
}

int main(int argc, char** argv, char** envp) {
    (void)envp;
    if (argc != 2) {
        print("usage: pkg ARCHIVE.ZIP\n");
        return 2;
    }

    int fd = sys_open(argv[1], FS_READ);
    if (fd < 0) {
        print("pkg: cannot open archive\n");
        return 1;
    }
    uint8_t* archive_bytes = (uint8_t*)sys_mmap_anon(PACKAGE_ARCHIVE_MAX);
    if (!archive_bytes) {
        sys_close(fd);
        print("pkg: cannot allocate archive buffer\n");
        return 1;
    }
    uint32_t size = 0;
    for (;;) {
        if (size == PACKAGE_ARCHIVE_MAX) {
            uint8_t extra;
            int count = sys_read(fd, &extra, 1);
            if (count == 0) break;
            sys_close(fd);
            print("pkg: archive exceeds size limit\n");
            return 1;
        }
        uint32_t capacity = PACKAGE_ARCHIVE_MAX - size;
        if (capacity > 4096) capacity = 4096;
        int count = sys_read(fd, archive_bytes + size, capacity);
        if (count < 0) {
            sys_close(fd);
            print("pkg: read failed\n");
            return 1;
        }
        if (count == 0) break;
        size += (uint32_t)count;
    }
    sys_close(fd);

    ZipArchive archive;
    if (zip_open(&archive, archive_bytes, size) != 0) {
        print("pkg: unsupported or invalid ZIP archive\n");
        return 1;
    }
    if (install_archive(&archive) != 0) {
        print("pkg: installation failed\n");
        return 1;
    }
    print("package installed under /PKG\n");
    return 0;
}