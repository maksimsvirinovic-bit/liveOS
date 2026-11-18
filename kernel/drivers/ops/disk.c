#include "disk.h"
#include <serial/serial.h>
#include <str.h>
#include <stddef.h>

extern int nvme_register(void);

static Disk g_disks[DISK_MAX];
static int  g_count = 0;

int disk_register(Disk* disk) {
    if (!disk || !disk->name || !disk->read_cb || !disk->write_cb) return -1;
    if (g_count >= DISK_MAX) return -1;
    g_disks[g_count] = *disk;
    serial_emergency_print("disk: registered ");
    serial_emergency_print(disk->name);
    serial_emergency_print(" (");
    serial_emergency_print_hex(disk->size_bytes / (1024 * 1024));
    serial_emergency_print(" MiB)\n");
    return g_count++;
}

int disk_subsystem_init(void) {
    g_count = 0;
    memset(g_disks, 0, sizeof(g_disks));

    int n = 0;
    n += nvme_register();

    serial_emergency_print("disk: subsystem init, ");
    serial_emergency_print_hex((uint64_t)n);
    serial_emergency_print(" disk(s)\n");
    return n;
}

int disk_count(void) { return g_count; }

const Disk* disk_get(int idx) {
    if (idx < 0 || idx >= g_count) return NULL;
    return &g_disks[idx];
}

int disk_read(int idx, uint64_t lba, uint32_t count, void* buf) {
    if (idx < 0 || idx >= g_count) return -1;
    return g_disks[idx].read_cb(lba, count, buf);
}
int disk_write(int idx, uint64_t lba, uint32_t count, const void* buf) {
    if (idx < 0 || idx >= g_count) return -1;
    return g_disks[idx].write_cb(lba, count, buf);
}
int disk_flush(int idx) {
    if (idx < 0 || idx >= g_count) return -1;
    if (!g_disks[idx].flush_cb) return 0;
    return g_disks[idx].flush_cb();
}