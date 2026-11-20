#include "nvme.hpp"
#include "paging.h"
#include "pmm.h"
#include "vmalloc.h"
#include "vmm.h"
#include <x86_64/pci.h>
#include <serial/serial.h>
#include <str.h>
#include <stddef.h>

namespace nvme {

uint32_t Controller::rd32(uint32_t off) {
    return *(volatile uint32_t*)(mmio_ + off);
}
uint64_t Controller::rd64(uint32_t off) {
    uint64_t lo = rd32(off);
    uint64_t hi = rd32(off + 4);
    return lo | (hi << 32);
}
void Controller::wr32(uint32_t off, uint32_t v) {
    *(volatile uint32_t*)(mmio_ + off) = v;
}
void Controller::wr64(uint32_t off, uint64_t v) {
    wr32(off, (uint32_t)v);
    wr32(off + 4, (uint32_t)(v >> 32));
}

void Controller::ring_sq_tail(uint16_t qid, uint16_t tail) {
    wr32(0x1000 + qid * 8, tail);
}
void Controller::ring_cq_head(uint16_t qid, uint16_t head) {
    wr32(0x1000 + qid * 8 + 4, head);
}

Controller::~Controller() {
}

int Controller::reset() {
    uint32_t cc = rd32(REG_CC);
    cc &= ~CC_EN;
    wr32(REG_CC, cc);

    for (int i = 0; i < 1000; i++) {
        if (!(rd32(REG_CSTS) & CSTS_RDY)) return 0;
        for (volatile int j = 0; j < 10000; j++);
    }
    serial_emergency_print("nvme: reset timeout\n");
    return -1;
}

int Controller::setup_admin_queue() {
    uint64_t sq_phys = pmm_alloc_pages(1);
    uint64_t cq_phys = pmm_alloc_pages(1);
    if (!sq_phys || !cq_phys) {
        serial_emergency_print("nvme: no pages for admin queue\n");
        return -1;
    }
    memset(phys_to_virt(sq_phys), 0, kPageSize);
    memset(phys_to_virt(cq_phys), 0, kPageSize);

    admin_sq_phys_ = sq_phys;
    admin_cq_phys_ = cq_phys;
    admin_sq_head_ = admin_sq_tail_ = 0;
    admin_cq_head_ = 0;
    admin_cq_phase_ = 1;

    uint32_t aqa = ((kAdminQueueDepth - 1) << 16) | (kAdminQueueDepth - 1);
    wr32(REG_AQA, aqa);
    wr64(REG_ASQ, sq_phys);
    wr64(REG_ACQ, cq_phys);
    return 0;
}

int Controller::enable() {
    uint32_t cc = CC_EN | CC_CSS_NVM | CC_MPS_4K | CC_AMS_RR
                | CC_IOSQES_64 | CC_IOCQES_16;
    wr32(REG_CC, cc);

    for (int i = 0; i < 1000; i++) {
        if (rd32(REG_CSTS) & CSTS_RDY) return 0;
        for (volatile int j = 0; j < 10000; j++);
    }
    serial_emergency_print("nvme: enable timeout\n");
    return -1;
}

int Controller::admin_cmd(Cmd& cmd) {
    uint16_t head = admin_sq_head_;
    uint16_t tail = admin_sq_tail_;

    if (((tail + 1) & (kAdminQueueDepth - 1)) == head) {
        serial_emergency_print("nvme: admin sq full\n");
        return -1;
    }

    Cmd* sq = (Cmd*)phys_to_virt(admin_sq_phys_);
    cmd.command_id = tail;
    sq[tail] = cmd;

    uint16_t new_tail = (uint16_t)((tail + 1) & (kAdminQueueDepth - 1));
    ring_sq_tail(0, new_tail);
    admin_sq_tail_ = new_tail;

    Cqe* cq = (Cqe*)phys_to_virt(admin_cq_phys_);
    uint16_t cq_idx = admin_cq_head_;
    uint32_t timeout = 100000000;

    while (timeout--) {
        uint16_t status = cq[cq_idx].status;
        if ((status & 1) == admin_cq_phase_) {
            admin_cq_head_ = (uint16_t)((cq_idx + 1) & (kAdminQueueDepth - 1));
            if (admin_cq_head_ == 0) admin_cq_phase_ ^= 1;
            ring_cq_head(0, admin_cq_head_);

            uint16_t sc = (status >> 1) & 0xFF;
            if (sc != 0) {
                serial_emergency_print("nvme: admin sc=");
                serial_emergency_print_hex(sc);
                serial_emergency_print("\n");
                return -1;
            }
            return 0;
        }
    }
    serial_emergency_print("nvme: admin timeout\n");
    return -1;
}

int Controller::identify(uint32_t nsid, uint32_t cns, uint64_t buf_phys) {
    Cmd cmd{};
    cmd.opcode = ADMIN_IDENTIFY;
    cmd.nsid = nsid;
    cmd.prp1 = buf_phys;
    cmd.cdw10 = cns;
    return admin_cmd(cmd);
}

int Controller::create_io_cq() {
    uint64_t cq_phys = pmm_alloc_pages(1);
    if (!cq_phys) return -1;
    memset(phys_to_virt(cq_phys), 0, kPageSize);

    Cmd cmd{};
    cmd.opcode = ADMIN_CREATE_CQ;
    cmd.prp1 = cq_phys;
    cmd.cdw10 = (kIoQueueId) | ((kIoQueueDepth - 1) << 16);
    cmd.cdw11 = 1;

    if (admin_cmd(cmd) != 0) return -1;

    io_cq_phys_ = cq_phys;
    io_cq_head_ = 0;
    io_cq_phase_ = 1;
    return 0;
}

int Controller::create_io_sq() {
    uint64_t sq_phys = pmm_alloc_pages(1);
    if (!sq_phys) return -1;
    memset(phys_to_virt(sq_phys), 0, kPageSize);

    Cmd cmd{};
    cmd.opcode = ADMIN_CREATE_SQ;
    cmd.prp1 = sq_phys;
    cmd.cdw10 = (kIoQueueId) | ((kIoQueueDepth - 1) << 16);
    cmd.cdw11 = (kIoQueueId << 16) | 1;

    if (admin_cmd(cmd) != 0) return -1;

    io_sq_phys_ = sq_phys;
    io_sq_head_ = io_sq_tail_ = 0;
    return 0;
}

int Controller::io_cmd(Cmd& cmd) {
    uint16_t head = io_sq_head_;
    uint16_t tail = io_sq_tail_;

    if (((tail + 1) & (kIoQueueDepth - 1)) == head) {
        serial_emergency_print("nvme: io sq full\n");
        return -1;
    }

    Cmd* sq = (Cmd*)phys_to_virt(io_sq_phys_);
    cmd.command_id = tail;
    sq[tail] = cmd;

    uint16_t new_tail = (uint16_t)((tail + 1) & (kIoQueueDepth - 1));
    __asm__ volatile("" ::: "memory");
    ring_sq_tail(kIoQueueId, new_tail);
    io_sq_tail_ = new_tail;

    volatile Cqe* cq = (volatile Cqe*)phys_to_virt(io_cq_phys_);
    uint16_t cq_idx = io_cq_head_;
    uint32_t timeout = 100000000;

    while (timeout--) {
        uint16_t status = cq[cq_idx].status;
        if ((status & 1) == io_cq_phase_) {
            uint16_t completed_sq_head = cq[cq_idx].sq_head;
            if (completed_sq_head >= kIoQueueDepth) {
                serial_emergency_print("nvme: invalid io sq head\n");
                return -1;
            }
            io_sq_head_ = completed_sq_head;

            io_cq_head_ = (uint16_t)((cq_idx + 1) & (kIoQueueDepth - 1));
            if (io_cq_head_ == 0) io_cq_phase_ ^= 1;
            ring_cq_head(kIoQueueId, io_cq_head_);

            uint16_t sc = (status >> 1) & 0xFF;
            if (sc != 0) {
                serial_emergency_print("nvme: io sc=");
                serial_emergency_print_hex(sc);
                serial_emergency_print("\n");
                return -1;
            }
            return 0;
        }
    }
    serial_emergency_print("nvme: io timeout\n");
    return -1;
}

int Controller::init() {
    PCIEntry* entry = pci_find_class_entry(0x01, 0x08, 0x02);
    if (!entry) {
        serial_emergency_print("nvme: controller not found\n");
        return -1;
    }
    bus_ = entry->bus;
    dev_ = entry->dev;
    func_ = entry->func;
    irq_ = pci_get_irq(entry);
    vendor_id_ = entry->device.vendor_id;
    bar0_phys_ = pci_get_bar64(&entry->device, 0);
    bar0_size_ = pci_bar_size(entry, 0);

    switch (vendor_id_) {
        case 0x144D: vendor_name_ = "Samsung"; break;
        case 0x2646: vendor_name_ = "Kingston"; break;
        case 0x1344: vendor_name_ = "Micron"; break;
        case 0x8086: vendor_name_ = "Intel"; break;
        case 0x1B36: vendor_name_ = "QEMU"; break;
        case 0x80EE: vendor_name_ = "VirtualBox"; break;
        default:     vendor_name_ = "unknown"; break;
    }

    serial_emergency_print("nvme: vendor=");
    serial_emergency_print(vendor_name_);
    serial_emergency_print(" BAR0=");
    serial_emergency_print_hex(bar0_phys_);
    serial_emergency_print(" size=");
    serial_emergency_print_hex(bar0_size_);
    serial_emergency_print(" irq=");
    serial_emergency_print_hex(irq_);
    serial_emergency_print("\n");

    if (!bar0_phys_ || !bar0_size_) return -1;

    pci_enable_bus_master(entry);

    uint64_t mmio_virt = vmalloc_reserve_mmio(bar0_size_);
    if (!mmio_virt) return -1;

    for (uint64_t off = 0; off < bar0_size_; off += kPageSize) {
        if (paging_map_4k(vmm_kernel()->pml4_phys,
                          bar0_phys_ + off,
                          mmio_virt + off,
                          PAGE_PRESENT | PAGE_WRITE | PAGE_NOCACHE) != 0) {
            serial_emergency_print("nvme: map MMIO failed\n");
            return -1;
        }
    }
    mmio_ = (volatile uint8_t*)mmio_virt;

    uint64_t cap = rd64(REG_CAP);
    uint32_t mqes = (uint32_t)(cap & 0xFFFF) + 1;
    uint32_t mpsmin = (uint32_t)((cap >> 48) & 0xF);
    uint32_t mpsmax = (uint32_t)((cap >> 52) & 0xF);
    uint32_t page_size = 1u << (12 + mpsmin);
    queue_depth_ = (uint16_t)(mqes > 256 ? 256 : mqes);

    serial_emergency_print("nvme: vs=");
    serial_emergency_print_hex(rd32(REG_VS));
    serial_emergency_print(" mqes=");
    serial_emergency_print_hex(mqes);
    serial_emergency_print(" mpsmin=");
    serial_emergency_print_hex(mpsmin);
    serial_emergency_print(" mpsmax=");
    serial_emergency_print_hex(mpsmax);
    serial_emergency_print(" page=");
    serial_emergency_print_hex(page_size);
    serial_emergency_print("\n");

    if (page_size != kPageSize) {
        serial_emergency_print("nvme: page size must be 4K\n");
        return -1;
    }

    if (reset() != 0) return -1;

    if (setup_admin_queue() != 0) return -1;

    if (enable() != 0) return -1;

    uint64_t idc_phys = pmm_alloc_pages(1);
    if (!idc_phys) return -1;
    memset(phys_to_virt(idc_phys), 0, kPageSize);
    if (identify(0, CNS_CTRL, idc_phys) != 0) return -1;

    uint8_t* idc = (uint8_t*)phys_to_virt(idc_phys);
    uint32_t nn = *(uint32_t*)(idc + 516);
    serial_emergency_print("nvme: NN=");
    serial_emergency_print_hex(nn);
    serial_emergency_print("\n");
    pmm_free_pages(idc_phys, 1);

    uint64_t idn_phys = pmm_alloc_pages(1);
    if (!idn_phys) return -1;
    memset(phys_to_virt(idn_phys), 0, kPageSize);
    if (identify(1, CNS_NS, idn_phys) != 0) return -1;

    uint8_t* idn = (uint8_t*)phys_to_virt(idn_phys);
    uint64_t nsze = *(uint64_t*)(idn + 0);
    uint8_t  flbas = idn[26] & 0xF;
    uint8_t  lbads = idn[128 + flbas * 4 + 2];
    sector_size_ = 1u << lbads;
    sector_count_ = nsze;
    pmm_free_pages(idn_phys, 1);

    serial_emergency_print("nvme: ns1 sectors=");
    serial_emergency_print_hex(nsze);
    serial_emergency_print(" sector_size=");
    serial_emergency_print_hex(sector_size_);
    serial_emergency_print(" (");
    serial_emergency_print_hex(nsze * sector_size_ / (1024 * 1024));
    serial_emergency_print(" MiB)\n");

    if (create_io_cq() != 0) return -1;
    if (create_io_sq() != 0) return -1;

    serial_emergency_print("nvme: ready\n");
    return 0;
}

int Controller::read(uint64_t lba, uint32_t count, void* buf) {
    if (count == 0) return 0;
    uint64_t bytes = (uint64_t)count * sector_size_;
    if (bytes > kPageSize) {
        serial_emergency_print("nvme: read too big (max 4K)\n");
        return -1;
    }

    uint64_t buf_phys = virt_to_phys(buf);
    if ((uint64_t)buf < PHYS_MAP_BASE) {
        serial_emergency_print("nvme: read buf not in direct map\n");
        return -1;
    }

    Cmd cmd{};
    cmd.opcode = IO_READ;
    cmd.nsid = 1;
    cmd.prp1 = buf_phys;
    cmd.cdw10 = (uint32_t)lba;
    cmd.cdw11 = (uint32_t)(lba >> 32);
    cmd.cdw12 = (count - 1) & 0xFFFF;

    return io_cmd(cmd);
}

int Controller::write(uint64_t lba, uint32_t count, const void* buf) {
    if (count == 0) return 0;
    uint64_t bytes = (uint64_t)count * sector_size_;
    if (bytes > kPageSize) {
        serial_emergency_print("nvme: write too big (max 4K)\n");
        return -1;
    }

    uint64_t buf_phys = virt_to_phys(buf);
    if ((uint64_t)buf < PHYS_MAP_BASE) {
        serial_emergency_print("nvme: write buf not in direct map\n");
        return -1;
    }

    Cmd cmd{};
    cmd.opcode = IO_WRITE;
    cmd.nsid = 1;
    cmd.prp1 = buf_phys;
    cmd.cdw10 = (uint32_t)lba;
    cmd.cdw11 = (uint32_t)(lba >> 32);
    cmd.cdw12 = (count - 1) & 0xFFFF;

    return io_cmd(cmd);
}

int Controller::flush() {
    Cmd cmd{};
    cmd.opcode = IO_FLUSH;
    cmd.nsid = 1;
    return io_cmd(cmd);
}

}