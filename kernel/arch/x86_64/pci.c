#include "pci.h"
#include "io.h"
#include <serial/serial.h>
#include <heap.h>
#include <str.h>
#include <stddef.h>

PCIEntry* pci_devices      = NULL;
int       pci_device_count = 0;


static inline uint32_t pci_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    return (1u << 31)
         | ((uint32_t)bus  << 16)
         | ((uint32_t)dev  << 11)
         | ((uint32_t)func << 8)
         | (offset & 0xFC);
}

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, offset));
    return inl(PCI_CONFIG_DATA);
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t v = pci_read32(bus, dev, func, offset & ~0x3);
    return (uint16_t)(v >> ((offset & 0x3) * 8));
}

uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t v = pci_read32(bus, dev, func, offset & ~0x3);
    return (uint8_t)(v >> ((offset & 0x3) * 8));
}

void pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t old = pci_read32(bus, dev, func, offset & ~0x3);
    uint32_t shift = (offset & 0x3) * 8;
    uint32_t mask  = 0xFFFFu << shift;
    uint32_t new_v = (old & ~mask) | ((uint32_t)value << shift);
    pci_write32(bus, dev, func, offset & ~0x3, new_v);
}

void pci_write8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint8_t value) {
    uint32_t old = pci_read32(bus, dev, func, offset & ~0x3);
    uint32_t shift = (offset & 0x3) * 8;
    uint32_t mask  = 0xFFu << shift;
    uint32_t new_v = (old & ~mask) | ((uint32_t)value << shift);
    pci_write32(bus, dev, func, offset & ~0x3, new_v);
}


static PCIDevice pci_read_device(uint8_t bus, uint8_t dev, uint8_t func) {
    PCIDevice d;
    uint32_t* p = (uint32_t*)&d;
    for (uint32_t i = 0; i < sizeof(PCIDevice) / 4; i++) {
        p[i] = pci_read32(bus, dev, func, i * 4);
    }
    return d;
}


PCIEntry* pci_find_class_entry(uint8_t class_code, uint8_t subclass, uint8_t prog_if) {
    for (PCIEntry* e = pci_devices; e; e = e->next) {
        if (e->device.class_code == class_code &&
            e->device.subclass   == subclass &&
            e->device.prog_if    == prog_if) {
            return e;
        }
    }
    return NULL;
}

PCIEntry* pci_find_vendor_device(uint16_t vendor_id, uint16_t device_id) {
    for (PCIEntry* e = pci_devices; e; e = e->next) {
        if (e->device.vendor_id == vendor_id &&
            e->device.device_id == device_id) {
            return e;
        }
    }
    return NULL;
}


uint64_t pci_get_bar64(PCIDevice* dev, int bar_index) {
    if (!dev || bar_index < 0 || bar_index > 5) return 0;
    uint32_t bar = dev->bar[bar_index];

    if (bar & 1) {
        return bar & ~0x3u;
    }

    uint64_t addr = bar & ~0xFu;

    if ((bar & 0x6) == 0x4 && bar_index + 1 < 6) {
        addr |= ((uint64_t)dev->bar[bar_index + 1]) << 32;
    }
    return addr;
}

uint64_t pci_bar_size(PCIEntry* entry, int bar_idx) {
    if (!entry || bar_idx < 0 || bar_idx > 5) return 0;

    uint8_t bus = entry->bus;
    uint8_t dev = entry->dev;
    uint8_t func = entry->func;
    uint8_t off = 0x10 + bar_idx * 4;

    uint32_t orig = pci_read32(bus, dev, func, off);
    pci_write32(bus, dev, func, off, 0xFFFFFFFF);
    uint32_t mask = pci_read32(bus, dev, func, off);
    pci_write32(bus, dev, func, off, orig);

    if (mask == 0 || mask == 0xFFFFFFFF) return 0;

    if (orig & 1) {
        mask &= ~0x3u;
        return (uint64_t)(~mask + 1) & 0xFFFF;
    } else {
        if ((orig & 0x6) == 0x4 && bar_idx + 1 < 6) {
            uint64_t mask64 = mask & ~0xFu;
            uint8_t off_hi = off + 4;
            uint32_t orig_hi = pci_read32(bus, dev, func, off_hi);
            pci_write32(bus, dev, func, off_hi, 0xFFFFFFFF);
            uint32_t mask_hi = pci_read32(bus, dev, func, off_hi);
            pci_write32(bus, dev, func, off_hi, orig_hi);
            mask64 |= (uint64_t)mask_hi << 32;
            return ~mask64 + 1;
        }
        uint32_t mask32 = mask & ~0xFu;
        return (uint32_t)(~mask32 + 1u);
    }
}


void pci_enable_bus_master(PCIEntry* entry) {
    if (!entry) return;
    uint16_t cmd = pci_read16(entry->bus, entry->dev, entry->func, 0x04);
    cmd |= PCI_CMD_MASTER | PCI_CMD_MEMORY;
    pci_write16(entry->bus, entry->dev, entry->func, 0x04, cmd);
    entry->device.command = cmd;
}

uint8_t pci_get_irq(PCIEntry* entry) {
    if (!entry) return 0xFF;
    return entry->device.interrupt_line;
}


static void pci_add_device(uint8_t bus, uint8_t dev, uint8_t func, PCIDevice d) {
    PCIEntry* e = (PCIEntry*)kmalloc(sizeof(PCIEntry));
    if (!e) {
        serial_emergency_print("pci: out of memory\n");
        return;
    }
    e->device = d;
    e->bus  = bus;
    e->dev  = dev;
    e->func = func;
    e->next = pci_devices;
    pci_devices = e;
    pci_device_count++;
}

static void pci_scan_bus(uint8_t bus) {
    for (uint8_t dev = 0; dev < 32; dev++) {
        uint32_t v = pci_read32(bus, dev, 0, 0);
        if (v == 0xFFFFFFFF) continue;

        for (uint8_t func = 0; func < 8; func++) {
            if (func > 0) {
                v = pci_read32(bus, dev, func, 0);
                if (v == 0xFFFFFFFF) continue;
            }

            PCIDevice d = pci_read_device(bus, dev, func);
            pci_add_device(bus, dev, func, d);

            if (d.class_code == PCI_CLASS_BRIDGE &&
                d.subclass   == PCI_SUBCLASS_BRIDGE_PCI) {
                uint32_t bus_num = pci_read32(bus, dev, func, 0x18);
                uint8_t secondary = (bus_num >> 8) & 0xFF;
                if (secondary != 0 && secondary != bus) {
                    pci_scan_bus(secondary);
                }
            }
        }
    }
}

void pci_init(void) {
    pci_devices = NULL;
    pci_device_count = 0;

    if (pci_read32(0, 0, 0, 0) == 0xFFFFFFFF) {
        serial_emergency_print("pci: no PCI bus\n");
        return;
    }

    pci_scan_bus(0);
    serial_emergency_print("pci: found ");
    serial_emergency_print_hex((uint64_t)pci_device_count);
    serial_emergency_print(" devices\n");
}


void pci_dump_entry(PCIEntry* e) {
    if (!e) return;
    serial_emergency_print("PCI ");
    serial_emergency_print_hex(e->bus);
    serial_emergency_print(":");
    serial_emergency_print_hex(e->dev);
    serial_emergency_print(".");
    serial_emergency_print_hex(e->func);
    serial_emergency_print(" vid=");
    serial_emergency_print_hex(e->device.vendor_id);
    serial_emergency_print(" did=");
    serial_emergency_print_hex(e->device.device_id);
    serial_emergency_print(" class=");
    serial_emergency_print_hex(e->device.class_code);
    serial_emergency_print("/");
    serial_emergency_print_hex(e->device.subclass);
    serial_emergency_print("/");
    serial_emergency_print_hex(e->device.prog_if);
    serial_emergency_print(" bar0=");
    serial_emergency_print_hex(pci_get_bar64(&e->device, 0));
    serial_emergency_print("\n");
}

void pci_list(void) {
    serial_emergency_print("PCI devices (");
    serial_emergency_print_hex((uint64_t)pci_device_count);
    serial_emergency_print("):\n");
    for (PCIEntry* e = pci_devices; e; e = e->next) {
        pci_dump_entry(e);
    }
}