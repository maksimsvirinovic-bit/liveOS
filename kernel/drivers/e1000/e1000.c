#include "e1000.h"
#include "net/netdev.h"
#include "x86_64/pci.h"
#include "x86_64/interrupts/irq.h"
#include "mm/paging.h"
#include "mm/pmm.h"
#include "mm/vmalloc.h"
#include <serial/serial.h>
#include <stddef.h>
#include <stdint.h>

#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID 0x100E

#define REG_CTRL   0x0000
#define REG_STATUS 0x0008
#define REG_EECD   0x0010
#define REG_EERD   0x0014
#define REG_ICR    0x00C0
#define REG_ITR    0x00C4
#define REG_IMS    0x00D0
#define REG_IMC    0x00D8
#define REG_RCTL   0x0100
#define REG_TCTL   0x0400
#define REG_TIPG   0x0410
#define REG_RDBAL  0x2800
#define REG_RDBAH  0x2804
#define REG_RDLEN  0x2808
#define REG_RDH    0x2810
#define REG_RDT    0x2818
#define REG_TDBAL  0x3800
#define REG_TDBAH  0x3804
#define REG_TDLEN  0x3808
#define REG_TDH    0x3810
#define REG_TDT    0x3818
#define REG_RAL    0x5400
#define REG_RAH    0x5404

#define CTRL_RST       (1u << 26)
#define RCTL_EN        (1u << 1)
#define RCTL_BAM       (1u << 15)
#define RCTL_SECRC     (1u << 26)
#define TCTL_EN        (1u << 1)
#define TCTL_PSP       (1u << 3)
#define TX_CMD_EOP     (1u << 0)
#define TX_CMD_IFCS    (1u << 1)
#define TX_CMD_RS      (1u << 3)
#define TX_STATUS_DD   (1u << 0)
#define RX_STATUS_DD   (1u << 0)
#define RX_STATUS_EOP  (1u << 1)
#define IRQ_TXDW       (1u << 0)
#define IRQ_LSC        (1u << 2)
#define IRQ_RXDMT0     (1u << 4)
#define IRQ_RXO        (1u << 6)
#define IRQ_RXT        (1u << 7)

#define E1000_RING_COUNT 16
#define E1000_RX_QUEUE_COUNT 8
#define E1000_BUFFER_SIZE 2048
#define E1000_MMIO_SIZE 0x20000
#define E1000_DMA_LIMIT 0x100000000ULL

typedef struct __attribute__((packed)) {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} E1000RxDescriptor;

typedef struct __attribute__((packed)) {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} E1000TxDescriptor;

typedef struct {
    PCIEntry* pci;
    volatile uint8_t* mmio;
    uint8_t irq;
    uint64_t rx_ring_phys;
    uint64_t tx_ring_phys;
    uint64_t rx_buffer_phys[E1000_RING_COUNT];
    uint64_t tx_buffer_phys[E1000_RING_COUNT];
    E1000RxDescriptor* rx_ring;
    E1000TxDescriptor* tx_ring;
    uint8_t rx_queue[E1000_RX_QUEUE_COUNT][E1000_BUFFER_SIZE];
    uint16_t rx_lengths[E1000_RX_QUEUE_COUNT];
    volatile uint32_t rx_head;
    volatile uint32_t rx_tail;
    volatile uint32_t rx_count;
    uint32_t rx_clean;
    uint32_t tx_clean;
    uint32_t tx_tail;
    NetDevice netdev;
} E1000State;

static E1000State g_e1000;

static uint32_t mmio_read(uint32_t reg) {
    return *(volatile uint32_t*)(g_e1000.mmio + reg);
}

static void mmio_write(uint32_t reg, uint32_t value) {
    *(volatile uint32_t*)(g_e1000.mmio + reg) = value;
}

static void zero_bytes(void* memory, uint32_t size) {
    uint8_t* bytes = (uint8_t*)memory;
    for (uint32_t i = 0; i < size; i++) bytes[i] = 0;
}

static uint64_t alloc_dma_page(void) {
    uint64_t physical = pmm_alloc_page();
    if (!physical || physical >= E1000_DMA_LIMIT) {
        if (physical) pmm_free_page(physical);
        return 0;
    }
    zero_bytes(phys_to_virt(physical), PAGE_SIZE);
    return physical;
}

static int map_mmio(uint64_t physical, uint64_t size) {
    uint64_t page_start = physical & ~(PAGE_SIZE - 1);
    uint64_t page_offset = physical - page_start;
    uint64_t map_size = (size + page_offset + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t virtual_start = vmalloc_reserve(map_size);
    if (!virtual_start) return -1;

    for (uint64_t offset = 0; offset < map_size; offset += PAGE_SIZE) {
        if (paging_map_4k(paging_kernel_pml4_phys(), page_start + offset,
                          virtual_start + offset,
                          PAGE_PRESENT | PAGE_WRITE | PAGE_NOCACHE) != 0) {
            return -1;
        }
    }
    g_e1000.mmio = (volatile uint8_t*)(uintptr_t)(virtual_start + page_offset);
    return 0;
}

static int setup_dma_rings(void) {
    g_e1000.rx_ring_phys = alloc_dma_page();
    g_e1000.tx_ring_phys = alloc_dma_page();
    if (!g_e1000.rx_ring_phys || !g_e1000.tx_ring_phys) return -1;

    g_e1000.rx_ring = (E1000RxDescriptor*)phys_to_virt(g_e1000.rx_ring_phys);
    g_e1000.tx_ring = (E1000TxDescriptor*)phys_to_virt(g_e1000.tx_ring_phys);

    for (uint32_t i = 0; i < E1000_RING_COUNT; i++) {
        g_e1000.rx_buffer_phys[i] = alloc_dma_page();
        g_e1000.tx_buffer_phys[i] = alloc_dma_page();
        if (!g_e1000.rx_buffer_phys[i] || !g_e1000.tx_buffer_phys[i]) return -1;
        g_e1000.rx_ring[i].address = g_e1000.rx_buffer_phys[i];
        g_e1000.rx_ring[i].status = 0;
        g_e1000.tx_ring[i].address = g_e1000.tx_buffer_phys[i];
        g_e1000.tx_ring[i].status = TX_STATUS_DD;
    }

    return 0;
}

static void read_mac_address(void) {
    uint32_t low = mmio_read(REG_RAL);
    uint32_t high = mmio_read(REG_RAH);
    for (uint32_t i = 0; i < 4; i++) g_e1000.netdev.mac[i] = (uint8_t)(low >> (i * 8));
    g_e1000.netdev.mac[4] = (uint8_t)high;
    g_e1000.netdev.mac[5] = (uint8_t)(high >> 8);
}

static int e1000_transmit(NetDevice* device, const void* frame, uint32_t length) {
    (void)device;
    if (!frame || length < 14 || length > NETDEV_FRAME_MAX) return -1;

    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    uint32_t index = g_e1000.tx_tail;
    E1000TxDescriptor* descriptor = &g_e1000.tx_ring[index];
    if (!(descriptor->status & TX_STATUS_DD)) {
        if (flags & (1u << 9)) __asm__ volatile("sti" ::: "memory");
        return -1;
    }

    uint8_t* destination = (uint8_t*)phys_to_virt(g_e1000.tx_buffer_phys[index]);
    const uint8_t* source = (const uint8_t*)frame;
    for (uint32_t i = 0; i < length; i++) destination[i] = source[i];

    descriptor->length = (uint16_t)length;
    descriptor->command = TX_CMD_EOP | TX_CMD_IFCS | TX_CMD_RS;
    descriptor->status = 0;
    __asm__ volatile("mfence" ::: "memory");
    g_e1000.tx_tail = (index + 1) % E1000_RING_COUNT;
    mmio_write(REG_TDT, g_e1000.tx_tail);
    if (flags & (1u << 9)) __asm__ volatile("sti" ::: "memory");
    return 0;
}

static int e1000_receive(NetDevice* device, void* frame, uint32_t capacity, uint32_t* length) {
    (void)device;
    if (!frame || !length) return -1;
    if (g_e1000.rx_count == 0) return 0;

    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    if (g_e1000.rx_count == 0) {
        if (flags & (1u << 9)) __asm__ volatile("sti" ::: "memory");
        return 0;
    }

    uint32_t index = g_e1000.rx_head;
    uint32_t packet_length = g_e1000.rx_lengths[index];
    if (packet_length > capacity) {
        g_e1000.rx_head = (index + 1) % E1000_RX_QUEUE_COUNT;
        g_e1000.rx_count--;
        if (flags & (1u << 9)) __asm__ volatile("sti" ::: "memory");
        return -1;
    }
    uint8_t* output = (uint8_t*)frame;
    for (uint32_t i = 0; i < packet_length; i++) output[i] = g_e1000.rx_queue[index][i];
    *length = packet_length;
    g_e1000.rx_head = (index + 1) % E1000_RX_QUEUE_COUNT;
    g_e1000.rx_count--;
    if (flags & (1u << 9)) __asm__ volatile("sti" ::: "memory");
    return 1;
}

static void e1000_irq_handler(uint32_t irq, void* context) {
    (void)irq;
    E1000State* state = (E1000State*)context;
    uint32_t cause = mmio_read(REG_ICR);
    if (!cause) return;

    if (cause & (IRQ_RXDMT0 | IRQ_RXO | IRQ_RXT)) {
        uint32_t index = state->rx_clean;
        for (uint32_t scanned = 0; scanned < E1000_RING_COUNT; scanned++) {
            E1000RxDescriptor* descriptor = &state->rx_ring[index];
            if (!(descriptor->status & RX_STATUS_DD)) break;

            uint32_t packet_length = descriptor->length;
            if ((descriptor->status & RX_STATUS_EOP) && packet_length >= 14 &&
                packet_length <= NETDEV_FRAME_MAX) {
                if (state->rx_count == E1000_RX_QUEUE_COUNT) {
                    state->rx_head = (state->rx_head + 1) % E1000_RX_QUEUE_COUNT;
                    state->rx_count--;
                }
                uint32_t queue_index = state->rx_tail;
                const uint8_t* source = (const uint8_t*)phys_to_virt(state->rx_buffer_phys[index]);
                for (uint32_t i = 0; i < packet_length; i++) state->rx_queue[queue_index][i] = source[i];
                state->rx_lengths[queue_index] = (uint16_t)packet_length;
                state->rx_tail = (queue_index + 1) % E1000_RX_QUEUE_COUNT;
                state->rx_count++;
            }

            descriptor->status = 0;
            mmio_write(REG_RDT, index);
            index = (index + 1) % E1000_RING_COUNT;
            state->rx_clean = index;
        }
    }

    if (cause & IRQ_TXDW) {
        while (state->tx_clean != state->tx_tail &&
               (state->tx_ring[state->tx_clean].status & TX_STATUS_DD)) {
            state->tx_clean = (state->tx_clean + 1) % E1000_RING_COUNT;
        }
    }

    if (cause & IRQ_LSC) (void)mmio_read(REG_STATUS);
}

static int initialize_controller(PCIEntry* pci) {
    g_e1000.pci = pci;
    g_e1000.irq = pci_get_irq(pci);
    uint64_t bar = pci_get_bar64(&pci->device, 0);
    if (!bar || (pci->device.bar[0] & 1) || g_e1000.irq >= IRQ_COUNT) return -1;

    if (map_mmio(bar, E1000_MMIO_SIZE) != 0) return -1;
    pci_enable_bus_master(pci);
    uint16_t command = pci_read16(pci->bus, pci->dev, pci->func, 0x04);
    pci_write16(pci->bus, pci->dev, pci->func, 0x04, command & ~(1u << 10));

    mmio_write(REG_IMC, 0xFFFFFFFFu);
    (void)mmio_read(REG_ICR);
    mmio_write(REG_CTRL, mmio_read(REG_CTRL) | CTRL_RST);
    uint32_t timeout = 1000000;
    while ((mmio_read(REG_CTRL) & CTRL_RST) && --timeout) __asm__ volatile("pause");
    if (!timeout) return -1;

    if (setup_dma_rings() != 0) return -1;
    read_mac_address();

    mmio_write(REG_RDBAL, (uint32_t)g_e1000.rx_ring_phys);
    mmio_write(REG_RDBAH, 0);
    mmio_write(REG_RDLEN, E1000_RING_COUNT * sizeof(E1000RxDescriptor));
    mmio_write(REG_RDH, 0);
    mmio_write(REG_RDT, E1000_RING_COUNT - 1);

    mmio_write(REG_TDBAL, (uint32_t)g_e1000.tx_ring_phys);
    mmio_write(REG_TDBAH, 0);
    mmio_write(REG_TDLEN, E1000_RING_COUNT * sizeof(E1000TxDescriptor));
    mmio_write(REG_TDH, 0);
    mmio_write(REG_TDT, 0);

    mmio_write(REG_TCTL, TCTL_EN | TCTL_PSP | (0x10u << 4) | (0x40u << 12));
    mmio_write(REG_TIPG, 10u | (8u << 10) | (6u << 20));
    mmio_write(REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);

    g_e1000.netdev.name[0] = 'e';
    g_e1000.netdev.name[1] = 'n';
    g_e1000.netdev.name[2] = '0';
    g_e1000.netdev.name[3] = '\0';
    g_e1000.netdev.mtu = 1500;
    g_e1000.netdev.private_data = &g_e1000;
    g_e1000.netdev.transmit = e1000_transmit;
    g_e1000.netdev.receive = e1000_receive;
    if (netdev_register(&g_e1000.netdev) != 0) return -1;
    if (irq_register(g_e1000.irq, e1000_irq_handler, &g_e1000) != 0) return -1;

    mmio_write(REG_ITR, 0);
    mmio_write(REG_IMS, IRQ_TXDW | IRQ_LSC | IRQ_RXDMT0 | IRQ_RXO | IRQ_RXT);
    (void)mmio_read(REG_STATUS);

    serial_emergency_print("e1000: registered en0 irq=");
    serial_emergency_print_hex(g_e1000.irq);
    serial_emergency_print(" mac=");
    for (uint32_t i = 0; i < 6; i++) {
        serial_emergency_print_hex(g_e1000.netdev.mac[i]);
        if (i != 5) serial_emergency_print(":");
    }
    serial_emergency_print("\n");
    return 0;
}

int e1000_init(void) {
    zero_bytes(&g_e1000, sizeof(g_e1000));
    PCIEntry* pci = pci_find_vendor_device(E1000_VENDOR_ID, E1000_DEVICE_ID);
    if (!pci) {
        serial_emergency_print("e1000: 82540EM not found\n");
        return -1;
    }
    if (initialize_controller(pci) != 0) {
        serial_emergency_print("e1000: initialization failed\n");
        return -1;
    }
    return 0;
}