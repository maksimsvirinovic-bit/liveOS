#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    uint32_t BootType;
    uint64_t KernelEntry;
    uint64_t KernelMinVaddr;
    uint64_t KernelPml4Phys;
    uint64_t KernelPhysBase;
    uint64_t KernelPhysSize; 
    uint64_t FbAddr;
    uint32_t Width;
    uint32_t Height;
    uint32_t Pitch;
    uint32_t Bpp;
    uint64_t InitramfsAddr;
    uint64_t InitramfsSize;
    uint64_t MemoryMapAddr;
    uint64_t MemoryMapSize;
    uint64_t MemoryMapDescriptorSize;
    uint32_t MemoryMapDescriptorVersion;
    uint64_t RuntimeServices;
} __attribute__((packed)) BOOT_INFO;

#ifdef __cplusplus
}
#endif

#endif