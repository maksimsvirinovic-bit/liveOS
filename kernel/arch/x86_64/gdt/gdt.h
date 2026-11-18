#ifndef GDT_H
#define GDT_H

#include <stdint.h>

#define GDT_NULL_SEL        0x00
#define GDT_KERNEL_CODE_SEL 0x08
#define GDT_KERNEL_DATA_SEL 0x10
#define GDT_USER_DATA_SEL   0x18
#define GDT_USER_CODE_SEL   0x20
#define GDT_TSS_SEL         0x28

#define GDT_ACCESS_PRESENT  0x80
#define GDT_ACCESS_DPL0     0x00
#define GDT_ACCESS_DPL3     0x60
#define GDT_ACCESS_S        0x10
#define GDT_ACCESS_EX       0x08
#define GDT_ACCESS_DC       0x04
#define GDT_ACCESS_RW       0x02
#define GDT_ACCESS_AC       0x01

#define GDT_GRAN_4KB        0x80
#define GDT_GRAN_32BIT      0x40
#define GDT_GRAN_64BIT      0x20

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) GDTEntry;

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_high32;
    uint32_t reserved;
} __attribute__((packed)) GDTEntryTSS;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) GDTPtr;

void gdt_init(void);

void gdt_set_tss(uint64_t tss_base, uint32_t tss_limit);

#endif