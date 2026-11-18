#ifndef VMM_H
#define VMM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "vma.h"
#include "x86_64/syscall.h"

#define USER_ABI_BASE       0x0000000000001000ULL
#define USER_ABI_SIZE       0x0000000000010000ULL
#define USER_CODE_BASE      0x0000000000400000ULL
#define USER_HEAP_BASE      0x0000000000800000ULL
#define USER_HEAP_LIMIT     0x0000000000F00000ULL
#define USER_STACK_TOP      0x0000000001000000ULL
#define USER_STACK_SIZE     (1ULL * 1024 * 1024)
#define USER_STACK_LIMIT    (USER_STACK_TOP - USER_STACK_SIZE)

typedef struct UserContext {
    uint64_t rip;
    uint64_t rsp;
    uint64_t rflags;
} UserContext;

typedef struct AddressSpace {
    uint64_t    pml4_phys;
    uint64_t    kernel_pml4_phys;
    VMAList     vmas;
    uint64_t    brk_start;
    uint64_t    brk_end;
    uint64_t    kstack_phys;
    uint64_t    kstack_top;
    UserContext user_ctx;
    int         user_active;
    uint64_t    kernel_return_rsp;
    uint64_t    kernel_return_rip;
    struct AddressSpace* next;
} AddressSpace;

int  vmm_init(uint64_t kernel_pml4_phys);

AddressSpace* vmm_create(void);
void          vmm_destroy(AddressSpace* as);

AddressSpace* vmm_kernel(void);

int  vmm_map(AddressSpace* as, uint64_t virt, uint64_t phys, uint64_t flags);
int  vmm_map_alloc(AddressSpace* as, uint64_t virt, uint64_t flags);
int  vmm_unmap(AddressSpace* as, uint64_t virt);
uint64_t vmm_get_phys(AddressSpace* as, uint64_t virt);
int  vmm_map_range_alloc(AddressSpace* as, uint64_t virt, uint64_t size,
                         uint64_t flags, VMAType type);
int  vmm_unmap_range(AddressSpace* as, uint64_t start, uint64_t end);

void          vmm_switch(AddressSpace* as);
AddressSpace* vmm_clone(AddressSpace* src);
int           vmm_replace(AddressSpace* as);
AddressSpace* vmm_current(void);
int  vmm_map_user_abi(AddressSpace* as);
int  vmm_alloc_user_stack(AddressSpace* as);
int  vmm_alloc_kernel_stack(AddressSpace* as);
int  vmm_enter_user(AddressSpace* as, uint64_t rip, uint64_t rsp);
int  vmm_resume_user(AddressSpace* as);
void vmm_save_user_context(AddressSpace* as, SyscallContext* ctx);

#ifdef __cplusplus
}
#endif

#endif