#ifndef USER_ABI_H
#define USER_ABI_H

#include <stdint.h>

#define USER_ABI_MAGIC   0x4C6976654F5331ULL
#define USER_ABI_VERSION 1

typedef struct UserABI {
    uint64_t magic;
    uint32_t version;
    uint32_t page_size;
    uint64_t kernel_tsc_freq;
    uint64_t boot_time_sec;
    uint64_t syscall_count;
    uint64_t syscall_table;
    uint64_t reserved[4096/8 - 7];
} __attribute__((packed)) UserABI;

int      user_abi_init(void);

uint64_t user_abi_phys(void);

#define USER_ABI_VADDR  0x0000000000001000ULL

#endif