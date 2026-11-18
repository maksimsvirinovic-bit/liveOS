#include "nvme.hpp"
#include <ops/disk.h>
#include <block.h>
#include "paging.h"
#include "pmm.h"

static nvme::Controller g_ctrl;
static bool            g_inited = false;
static BlockDevice     g_block_device{};

static int read_with_bounce(nvme::Controller* ctrl, uint64_t lba,
                            uint32_t count, void* buf) {
    if (!ctrl || (!buf && count != 0)) return -1;
    if (count == 0) return 0;

    uint32_t sector_size = ctrl->sector_size();
    if (sector_size == 0) return -1;
    uint32_t sectors_per_page = PAGE_SIZE / sector_size;
    if (sectors_per_page == 0) return -1;

    uint64_t bounce_phys = pmm_alloc_page();
    if (!bounce_phys) return -1;
    uint8_t* bounce = (uint8_t*)phys_to_virt(bounce_phys);
    uint8_t* dst = (uint8_t*)buf;

    uint32_t done = 0;
    int result = 0;
    while (done < count) {
        uint32_t chunk = count - done;
        if (chunk > sectors_per_page) chunk = sectors_per_page;
        result = ctrl->read(lba + done, chunk, bounce);
        if (result != 0) break;

        uint64_t bytes = (uint64_t)chunk * sector_size;
        uint64_t offset = (uint64_t)done * sector_size;
        for (uint64_t i = 0; i < bytes; i++) dst[offset + i] = bounce[i];
        done += chunk;
    }

    pmm_free_page(bounce_phys);
    return result;
}

static int write_with_bounce(nvme::Controller* ctrl, uint64_t lba,
                             uint32_t count, const void* buf) {
    if (!ctrl || (!buf && count != 0)) return -1;
    if (count == 0) return 0;

    uint32_t sector_size = ctrl->sector_size();
    if (sector_size == 0) return -1;
    uint32_t sectors_per_page = PAGE_SIZE / sector_size;
    if (sectors_per_page == 0) return -1;

    uint64_t bounce_phys = pmm_alloc_page();
    if (!bounce_phys) return -1;
    uint8_t* bounce = (uint8_t*)phys_to_virt(bounce_phys);
    const uint8_t* src = (const uint8_t*)buf;

    uint32_t done = 0;
    int result = 0;
    while (done < count) {
        uint32_t chunk = count - done;
        if (chunk > sectors_per_page) chunk = sectors_per_page;

        uint64_t bytes = (uint64_t)chunk * sector_size;
        uint64_t offset = (uint64_t)done * sector_size;
        for (uint64_t i = 0; i < bytes; i++) bounce[i] = src[offset + i];
        result = ctrl->write(lba + done, chunk, bounce);
        if (result != 0) break;
        done += chunk;
    }

    pmm_free_page(bounce_phys);
    return result;
}

static int read_cb(uint64_t lba, uint32_t count, void* buf) {
    return read_with_bounce(&g_ctrl, lba, count, buf);
}
static int write_cb(uint64_t lba, uint32_t count, const void* buf) {
    return write_with_bounce(&g_ctrl, lba, count, buf);
}
static int flush_cb() {
    return g_ctrl.flush();
}

static int block_read_cb(BlockDevice* dev, uint64_t lba, uint32_t count, void* buf) {
    auto* ctrl = static_cast<nvme::Controller*>(dev->private_data);
    return read_with_bounce(ctrl, lba, count, buf);
}
static int block_write_cb(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buf) {
    auto* ctrl = static_cast<nvme::Controller*>(dev->private_data);
    return write_with_bounce(ctrl, lba, count, buf);
}
static int block_flush_cb(BlockDevice* dev) {
    auto* ctrl = static_cast<nvme::Controller*>(dev->private_data);
    return ctrl ? ctrl->flush() : -1;
}

extern "C" int nvme_register(void) {
    if (g_inited) return 0;
    if (g_ctrl.init() != 0) return 0;
    g_block_device.name         = "nvme0";
    g_block_device.sector_size  = g_ctrl.sector_size();
    g_block_device.sector_count = g_ctrl.sector_count();
    g_block_device.private_data = &g_ctrl;
    g_block_device.read         = block_read_cb;
    g_block_device.write        = block_write_cb;
    g_block_device.flush        = block_flush_cb;
    g_block_device.next         = nullptr;

    if (block_register(&g_block_device) != 0) return 0;

    Disk d{};
    d.name         = "nvme0";
    d.sector_size  = g_ctrl.sector_size();
    d.sector_count = g_ctrl.sector_count();
    d.size_bytes   = d.sector_size * d.sector_count;
    d.read_cb      = read_cb;
    d.write_cb     = write_cb;
    d.flush_cb     = flush_cb;
    d.drv_data      = &g_ctrl;

    if (disk_register(&d) < 0) {
        block_unregister(&g_block_device);
        return 0;
    }
    g_inited = true;
    return 1;
}