#ifndef PCI_H
#define PCI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>


#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC


#define PCI_CLASS_STORAGE     0x01
#define PCI_SUBCLASS_NVME     0x08
#define PCI_PROGIF_NVME       0x02

#define PCI_CLASS_NETWORK     0x02
#define PCI_CLASS_DISPLAY     0x03
#define PCI_CLASS_MULTIMEDIA  0x04
#define PCI_CLASS_MEMORY      0x05
#define PCI_CLASS_BRIDGE      0x06
#define PCI_CLASS_SERIAL      0x07

#define PCI_SUBCLASS_BRIDGE_PCI 0x04


#define PCI_CMD_IO        0x0001
#define PCI_CMD_MEMORY    0x0002
#define PCI_CMD_MASTER    0x0004
#define PCI_CMD_SPECIAL   0x0008
#define PCI_CMD_INVALIDATE 0x0010
#define PCI_CMD_VGA_PALETTE 0x0020
#define PCI_CMD_PARITY    0x0040
#define PCI_CMD_WAIT      0x0080
#define PCI_CMD_SERR      0x0100
#define PCI_CMD_FAST_BACK 0x0200
#define PCI_CMD_DISABLE   0x0400


#define PCI_HEADER_TYPE_MASK   0x7F
#define PCI_HEADER_TYPE_DEVICE 0x00
#define PCI_HEADER_TYPE_BRIDGE 0x01
#define PCI_HEADER_TYPE_MULTI  0x80


typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;
    uint8_t  revision_id;
    uint8_t  prog_if;
    uint8_t  subclass;
    uint8_t  class_code;
    uint8_t  cache_line_size;
    uint8_t  latency_timer;
    uint8_t  header_type;
    uint8_t  bist;
    uint32_t bar[6];
    uint32_t cardbus_cis_ptr;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_id;
    uint32_t expansion_rom_base;
    uint8_t  capabilities_ptr;
    uint8_t  reserved1[3];
    uint32_t reserved2;
    uint8_t  interrupt_line;
    uint8_t  interrupt_pin;
    uint8_t  min_grant;
    uint8_t  max_latency;
} __attribute__((packed)) PCIDevice;


typedef struct PCIEntry {
    PCIDevice device;
    uint8_t   bus;
    uint8_t   dev;
    uint8_t   func;
    struct PCIEntry* next;
} PCIEntry;

extern PCIEntry* pci_devices;
extern int       pci_device_count;


void pci_init(void);


uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint8_t  pci_read8 (uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
void     pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);
void     pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t value);
void     pci_write8 (uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint8_t value);


PCIEntry* pci_find_class_entry(uint8_t class_code, uint8_t subclass, uint8_t prog_if);
PCIEntry* pci_find_vendor_device(uint16_t vendor_id, uint16_t device_id);


uint64_t pci_get_bar64(PCIDevice* dev, int bar_index);
uint64_t pci_bar_size(PCIEntry* entry, int bar_idx);


void    pci_enable_bus_master(PCIEntry* entry);
uint8_t pci_get_irq(PCIEntry* entry);


void pci_list(void);
void pci_dump_entry(PCIEntry* entry);

#ifdef __cplusplus
}
#endif

#endif