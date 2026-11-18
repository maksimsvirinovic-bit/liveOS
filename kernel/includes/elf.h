#ifndef ELF_H
#define ELF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "vmm.h"

#define ELF_MAGIC       0x464C457F
#define ELF_CLASS_64    2
#define ELF_DATA_LE     1
#define ELF_MACHINE_X86_64 0x3E

#define PT_LOAD         1
#define PT_DYNAMIC      2
#define PT_INTERP       3

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) Elf64_Phdr;

typedef struct {
  uint32_t	sh_name;
  uint32_t	sh_type;
  uint64_t	sh_flags;
  uint64_t	sh_addr;
  uint64_t	sh_offset;
  uint64_t	sh_size;
  uint32_t	sh_link;
  uint32_t	sh_info;
  uint64_t	sh_addralign;
  uint64_t	sh_entsize;
} __attribute__((packed)) Elf64_Shdr;

typedef struct {
    char     name[64];
    uint64_t addr;
    void**   ptr;
    uint64_t flags;
    uint32_t version;
    uint32_t reserved;
} __attribute__((packed)) ElfDynSymbol;

typedef struct {
    uint32_t count;
    uint32_t reserved;
    ElfDynSymbol symbols[];
} __attribute__((packed)) ElfDynSymSection;

typedef struct {
    uint64_t entry;
    uint64_t base;
    uint64_t size;
    int      is_user;
    AddressSpace* as;
} ElfLoaded;

int elf_load(const void* elf_data, uint64_t elf_size,
             AddressSpace* as, ElfLoaded* out);

void elf_unload(ElfLoaded* loaded, AddressSpace* as);

#ifdef __cplusplus
}
#endif

#endif