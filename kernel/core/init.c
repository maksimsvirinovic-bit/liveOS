#include "init.h"
#include "fs.h"
#include "paging.h"
#include "vmm.h"
#include "elf.h"
#include "heap.h"
#include "serial/serial.h"
#include <stddef.h>

Process* init_load_program(const char* path, int pid, const char* name) {
    if (!path) return NULL;

    File* f = NULL;
    if (fs_open(path, FS_READ, &f) != 0) {
        serial_emergency_print("init: cannot open ");
        serial_emergency_print(path);
        serial_emergency_print("\n");
        return NULL;
    }

    uint64_t fsize = f->size;
    if (fsize == 0 || fsize > 16 * 1024 * 1024) {
        fs_close(f);
        return NULL;
    }

    uint8_t* elf_buf = (uint8_t*)kmalloc(fsize);
    if (!elf_buf) { fs_close(f); return NULL; }

    int64_t n = fs_read(f, elf_buf, fsize);
    fs_close(f);
    if (n != (int64_t)fsize) {
        kfree(elf_buf);
        return NULL;
    }

    AddressSpace* as = vmm_create();
    if (!as) { kfree(elf_buf); return NULL; }
    if (vmm_alloc_kernel_stack(as) != 0) { vmm_destroy(as); kfree(elf_buf); return NULL; }
    if (vmm_alloc_user_stack(as) != 0)   { vmm_destroy(as); kfree(elf_buf); return NULL; }
    if (vmm_map_user_abi(as) != 0)       { vmm_destroy(as); kfree(elf_buf); return NULL; }

    ElfLoaded el;
    if (elf_load(elf_buf, fsize, as, &el) != 0) {
        vmm_destroy(as);
        kfree(elf_buf);
        return NULL;
    }
    kfree(elf_buf);

    Process* p = process_create(0, as, name ? name : "user");
    if (!p) { vmm_destroy(as); return NULL; }
    if (pid > 0) p->pid = pid;

    uint64_t new_rsp = USER_STACK_TOP;

    new_rsp -= 8;
    *(uint64_t*)(phys_to_virt(paging_get_phys(as->pml4_phys, new_rsp) + (new_rsp & 0xFFF))) = 0;
    new_rsp -= 8;
    *(uint64_t*)(phys_to_virt(paging_get_phys(as->pml4_phys, new_rsp) + (new_rsp & 0xFFF))) = 0;
    new_rsp -= 8;
    *(uint64_t*)(phys_to_virt(paging_get_phys(as->pml4_phys, new_rsp) + (new_rsp & 0xFFF))) = 0;

    process_setup_initial_frame(p, el.entry, new_rsp, 0x23, 0x1B, 0x202);

    serial_emergency_print("init: loaded ");
    serial_emergency_print(path);
    serial_emergency_print(" as pid=");
    serial_emergency_print_hex((uint64_t)p->pid);
    serial_emergency_print(" entry=");
    serial_emergency_print_hex(el.entry);
    serial_emergency_print("\n");

    return p;
}