#include "elf.h"
#include "vmm.h"
#include "pmm.h"
#include "paging.h"
#include "symbol.h"
#include <serial/serial.h>
#include <stddef.h>
#include <str.h>

#define ELF_USER_LIMIT 0x0000800000000000ULL
#define ELF_SHT_NOBITS 8

static int elf_range_valid(uint64_t total, uint64_t offset, uint64_t length) {
    return offset <= total && length <= total - offset;
}

static int elf_tables_valid(const uint8_t* data, uint64_t size, const Elf64_Ehdr* eh) {
    if (eh->e_ehsize < sizeof(*eh) || eh->e_phentsize != sizeof(Elf64_Phdr)) return 0;
    if (eh->e_phnum && !elf_range_valid(size, eh->e_phoff,
                                         (uint64_t)eh->e_phnum * sizeof(Elf64_Phdr))) return 0;
    if (!eh->e_shnum) return eh->e_shoff == 0 || eh->e_shentsize == sizeof(Elf64_Shdr);
    if (eh->e_shentsize != sizeof(Elf64_Shdr) || eh->e_shstrndx >= eh->e_shnum ||
        !elf_range_valid(size, eh->e_shoff, (uint64_t)eh->e_shnum * sizeof(Elf64_Shdr))) return 0;

    const Elf64_Shdr* sections = (const Elf64_Shdr*)(data + eh->e_shoff);
    for (uint16_t i = 0; i < eh->e_shnum; i++) {
        if (sections[i].sh_type != ELF_SHT_NOBITS &&
            !elf_range_valid(size, sections[i].sh_offset, sections[i].sh_size)) return 0;
    }
    const Elf64_Shdr* names = &sections[eh->e_shstrndx];
    return names->sh_type != ELF_SHT_NOBITS &&
           elf_range_valid(size, names->sh_offset, names->sh_size);
}

static int elf_section_name_eq(const uint8_t* data, const Elf64_Ehdr* eh,
                               const Elf64_Shdr* section,
                               const char* expected) {
    const Elf64_Shdr* sections = (const Elf64_Shdr*)(data + eh->e_shoff);
    const Elf64_Shdr* names = &sections[eh->e_shstrndx];
    if (section->sh_name >= names->sh_size) return 0;
    uint64_t remaining = names->sh_size - section->sh_name;
    const char* name = (const char*)(data + names->sh_offset + section->sh_name);
    size_t expected_len = strlen(expected);
    return expected_len < remaining && name[expected_len] == '\0' &&
           memcmp(name, expected, expected_len) == 0;
}

static int elf_check_header(const Elf64_Ehdr* eh) {
    if (*(uint32_t*)eh->e_ident != ELF_MAGIC) {
        serial_emergency_print("elf: bad magic\n");
        return -1;
    }
    if (eh->e_ident[4] != ELF_CLASS_64) {
        serial_emergency_print("elf: not 64-bit\n");
        return -1;
    }
    if (eh->e_ident[5] != ELF_DATA_LE) {
        serial_emergency_print("elf: not LE\n");
        return -1;
    }
    if (eh->e_machine != ELF_MACHINE_X86_64) {
        serial_emergency_print("elf: not x86_64\n");
        return -1;
    }
    return 0;
}

static int elf_load_segments(const uint8_t* data, uint64_t size,
                             const Elf64_Ehdr* eh,
                             AddressSpace* as, ElfLoaded* out) {
    const Elf64_Phdr* ph = (const Elf64_Phdr*)(data + eh->e_phoff);

    uint64_t min_vaddr = ~0ULL;
    uint64_t max_vaddr = 0;
    int entry_is_executable = 0;

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;

        if (ph[i].p_filesz > ph[i].p_memsz ||
            !elf_range_valid(size, ph[i].p_offset, ph[i].p_filesz) ||
            ph[i].p_vaddr >= ELF_USER_LIMIT ||
            ph[i].p_memsz > ELF_USER_LIMIT - ph[i].p_vaddr ||
            ph[i].p_vaddr + ph[i].p_memsz > ELF_USER_LIMIT - (PAGE_SIZE - 1) ||
            ((ph[i].p_flags & (PF_W | PF_X)) == (PF_W | PF_X))) return -1;

        if (ph[i].p_memsz == 0) continue;

        uint64_t vstart = ph[i].p_vaddr & ~(PAGE_SIZE - 1);
        uint64_t vend   = (ph[i].p_vaddr + ph[i].p_memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint64_t pages = (vend - vstart) / PAGE_SIZE;
        if (vma_find_overlap(&as->vmas, vstart, vend)) return -1;

        if ((ph[i].p_flags & PF_X) && eh->e_entry >= ph[i].p_vaddr &&
            eh->e_entry - ph[i].p_vaddr < ph[i].p_memsz) entry_is_executable = 1;

        if (ph[i].p_vaddr < min_vaddr) min_vaddr = ph[i].p_vaddr;
        if (ph[i].p_vaddr + ph[i].p_memsz > max_vaddr)
            max_vaddr = ph[i].p_vaddr + ph[i].p_memsz;

        uint64_t flags = PAGE_PRESENT | PAGE_USER;
        if (ph[i].p_flags & PF_W) flags |= PAGE_WRITE;
        if (!(ph[i].p_flags & PF_X)) flags |= PAGE_NX;

        for (uint64_t p = 0; p < pages; p++) {
            uint64_t virt = vstart + p * PAGE_SIZE;
            uint64_t phys = pmm_alloc_page();
            if (!phys) {
                serial_emergency_print("elf: no phys\n");
                return -1;
            }
            if (paging_map_4k(as->pml4_phys, phys, virt, flags) != 0) {
                pmm_free_page(phys);
                return -1;
            }
        }

        uint64_t src = ph[i].p_offset;
        uint64_t dst_vaddr = ph[i].p_vaddr;
        uint64_t filesz = ph[i].p_filesz;
        uint64_t memsz = ph[i].p_memsz;

        for (uint64_t i2 = 0; i2 < filesz; i2++) {
            uint64_t virt = dst_vaddr + i2;
            uint64_t phys = paging_get_phys(as->pml4_phys, virt);
            if (!phys) {
                serial_emergency_print("elf: no phys for copy\n");
                return -1;
            }
            ((uint8_t*)phys_to_virt(phys))[virt & (PAGE_SIZE - 1)] = data[src + i2];
        }

        for (uint64_t i2 = filesz; i2 < memsz; i2++) {
            uint64_t virt = dst_vaddr + i2;
            uint64_t phys = paging_get_phys(as->pml4_phys, virt);
            if (!phys) continue;
            ((uint8_t*)phys_to_virt(phys))[virt & (PAGE_SIZE - 1)] = 0;
        }

        if (vma_add(&as->vmas, vstart, vend, flags, VMA_FILE) != 0) return -1;
    }

    if (min_vaddr == ~0ULL || !entry_is_executable || eh->e_entry >= ELF_USER_LIMIT) return -1;

    out->base = min_vaddr & ~(PAGE_SIZE - 1);
    out->size = max_vaddr - out->base;
    return 0;
}

static int elf_process_exports(const uint8_t* data, const Elf64_Ehdr* eh,
                               AddressSpace* as, ElfLoaded* out) {
    (void)as;
    if (eh->e_shoff == 0 || eh->e_shnum == 0) return 0;

    const Elf64_Shdr* sh = (const Elf64_Shdr*)(data + eh->e_shoff);

    for (uint16_t i = 0; i < eh->e_shnum; i++) {
        if (!elf_section_name_eq(data, eh, &sh[i], ".exp")) continue;

        uint64_t count = sh[i].sh_size / sizeof(ElfDynSymbol);
        const ElfDynSymbol* syms = (const ElfDynSymbol*)(data + sh[i].sh_offset);

        serial_emergency_print("elf: .exp has ");
        serial_emergency_print_hex(count);
        serial_emergency_print(" symbols\n");

        for (uint64_t s = 0; s < count; s++) {
            const ElfDynSymbol* ds = &syms[s];

            if (!memchr(ds->name, '\0', sizeof(ds->name))) return -1;
            uint64_t abs_addr = ds->addr;
            uint64_t flags = ds->flags | (out->is_user ? SYM_USER : SYM_KERNEL);

            if (out->is_user || symbol_register(ds->name, abs_addr, flags, ds->version) != 0) {
                serial_emergency_print("elf: export failed: ");
                serial_emergency_print(ds->name);
                serial_emergency_print("\n");
            } else {
                serial_emergency_print("elf: export ");
                serial_emergency_print(ds->name);
                serial_emergency_print(" = ");
                serial_emergency_print_hex(abs_addr);
                serial_emergency_print("\n");
            }
        }
    }
    return 0;
}

static int elf_process_imports(const uint8_t* data, const Elf64_Ehdr* eh,
                               AddressSpace* as, ElfLoaded* out) {
    (void)out;
    if (eh->e_shoff == 0 || eh->e_shnum == 0) return 0;

    const Elf64_Shdr* sh = (const Elf64_Shdr*)(data + eh->e_shoff);

    for (uint16_t i = 0; i < eh->e_shnum; i++) {
        if (!elf_section_name_eq(data, eh, &sh[i], ".imp")) continue;

        uint64_t count = sh[i].sh_size / sizeof(ElfDynSymbol);
        const ElfDynSymbol* syms = (const ElfDynSymbol*)(data + sh[i].sh_offset);

        serial_emergency_print("elf: .imp has ");
        serial_emergency_print_hex(count);
        serial_emergency_print(" symbols\n");

        for (uint64_t s = 0; s < count; s++) {
            const ElfDynSymbol* ds = &syms[s];
            if (!memchr(ds->name, '\0', sizeof(ds->name))) return -1;
            uint64_t resolved = symbol_resolve(ds->name);
            if (!resolved) {
                serial_emergency_print("elf: import unresolved: ");
                serial_emergency_print(ds->name);
                serial_emergency_print("\n");
                return -1;
            }

            if (ds->ptr) {
                uint64_t import_ptr = (uint64_t)(uintptr_t)ds->ptr;
                if (import_ptr >= ELF_USER_LIMIT || sizeof(uint64_t) > ELF_USER_LIMIT - import_ptr ||
                    !paging_user_access(as->pml4_phys, import_ptr, 1) ||
                    !paging_user_access(as->pml4_phys, import_ptr + sizeof(uint64_t) - 1, 1)) {
                    serial_emergency_print("elf: import ptr outside writable user mapping\n");
                    return -1;
                }
                uint64_t ptr_phys = paging_get_phys(as->pml4_phys, import_ptr);
                if (!ptr_phys) {
                    serial_emergency_print("elf: import ptr not mapped\n");
                    return -1;
                }
                *(uint64_t*)phys_to_virt(ptr_phys + (import_ptr & (PAGE_SIZE - 1))) = resolved;
            }

            serial_emergency_print("elf: import ");
            serial_emergency_print(ds->name);
            serial_emergency_print(" = ");
            serial_emergency_print_hex(resolved);
            serial_emergency_print("\n");
        }
    }
    return 0;
}

int elf_load(const void* elf_data, uint64_t elf_size,
             AddressSpace* as, ElfLoaded* out) {
    if (!elf_data || elf_size < sizeof(Elf64_Ehdr)) return -1;
    if (!out) return -1;

    const uint8_t* data = (const uint8_t*)elf_data;
    const Elf64_Ehdr* eh = (const Elf64_Ehdr*)data;

    if (elf_check_header(eh) != 0 || !elf_tables_valid(data, elf_size, eh)) {
        serial_emergency_print("elf: invalid table bounds\n");
        return -1;
    }

    if (eh->e_phnum == 0) {
        serial_emergency_print("elf: no program headers\n");
        return -1;
    }

    if (!as) {
        serial_emergency_print("elf: only user AS supported for now\n");
        return -1;
    }

    out->entry = eh->e_entry;
    out->is_user = 1;
    out->as = as;

    if (elf_load_segments(data, elf_size, eh, as, out) != 0) return -1;
    if (elf_process_exports(data, eh, as, out) != 0) return -1;
    if (elf_process_imports(data, eh, as, out) != 0) return -1;

    serial_emergency_print("elf: loaded entry=");
    serial_emergency_print_hex(out->entry);
    serial_emergency_print(" base=");
    serial_emergency_print_hex(out->base);
    serial_emergency_print(" size=");
    serial_emergency_print_hex(out->size);
    serial_emergency_print("\n");

    return 0;
}

void elf_unload(ElfLoaded* loaded, AddressSpace* as) {
    (void)loaded; (void)as;
}