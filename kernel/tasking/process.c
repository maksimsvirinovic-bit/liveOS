#include "process.h"
#include "scheduler.h"
#include "vmm.h"
#include "vmalloc.h"
#include "heap.h"
#include "paging.h"
#include "x86_64/gdt/tss.h"
#include "x86_64/percpu.h"
#include "elf.h"
#include "serial/serial.h"
#include <str.h>
#include <stddef.h>

static Process* g_processes    = NULL;
static Process* g_current      = NULL;
static int      g_next_pid     = 1;
static uint32_t g_fat32_root_cluster = 2;

void process_init(void) {
    g_processes = NULL;
    g_current = NULL;
    g_next_pid = 1;
}

Process* process_get_current(void) { return g_current; }
void     process_set_current(Process* p) { g_current = p; }

void process_setup_initial_frame(Process* p, uint64_t rip, uint64_t rsp,
                                  uint64_t cs, uint64_t ss, uint64_t rflags) {
    uint64_t* frame = (uint64_t*)p->kstack_top;

    *(--frame) = ss;
    *(--frame) = rsp;
    *(--frame) = rflags;
    *(--frame) = cs;
    *(--frame) = rip;
    *(--frame) = 0;
    *(--frame) = 0;
    for (int i = 0; i < 15; i++) *(--frame) = 0;

    p->saved_rsp = (uint64_t)frame;
}

int copy_from_user_string(char* dst, const char* user_src, uint64_t max_size) {
    Process* proc = process_get_current();
    if (!proc || !proc->as || !user_src || !dst || max_size < 2) return -1;
    uint64_t src = (uint64_t)user_src;
    const uint64_t user_limit = 0x0000800000000000ULL;
    if (src >= user_limit || max_size - 1 > user_limit - src) return -1;

    uint64_t copied = 0;
    while (copied < max_size - 1) {
        uint64_t address = src + copied;
        if (!paging_user_access(proc->as->pml4_phys, address, 0)) return -1;
        uint64_t chunk = PAGE_SIZE - (address & (PAGE_SIZE - 1));
        if (chunk > max_size - 1 - copied) chunk = max_size - 1 - copied;
        for (uint64_t i = 0; i < chunk; i++) {
            char c = *(const char*)(uintptr_t)(address + i);
            dst[copied + i] = c;
            if (c == '\0') return 0;
        }
        copied += chunk;
    }
    dst[max_size - 1] = '\0';
    return -1;
}

static uint64_t build_user_stack(AddressSpace* as, char** argv, char** envp) {
    uint64_t rsp = USER_STACK_TOP;
    int argc = 0, envc = 0;
    if (argv) while (argv[argc]) argc++;
    if (envp) while (envp[envc]) envc++;

    uint64_t argv_addrs[64];
    uint64_t envp_addrs[64];

    for (int i = envc - 1; i >= 0; i--) {
        int len = strlen(envp[i]) + 1;
        rsp -= len;
        if (rsp < USER_STACK_LIMIT + 256) return 0;
        uint64_t phys = paging_get_phys(as->pml4_phys, rsp);
        if (!phys) return 0;
        memcpy(phys_to_virt(phys + (rsp & 0xFFF)), envp[i], len);
        envp_addrs[i] = rsp;
    }
    for (int i = argc - 1; i >= 0; i--) {
        int len = strlen(argv[i]) + 1;
        rsp -= len;
        if (rsp < USER_STACK_LIMIT + 256) return 0;
        uint64_t phys = paging_get_phys(as->pml4_phys, rsp);
        if (!phys) return 0;
        memcpy(phys_to_virt(phys + (rsp & 0xFFF)), argv[i], len);
        argv_addrs[i] = rsp;
    }

    rsp &= ~0xFULL;
    rsp -= 8;
    if (rsp < USER_STACK_LIMIT + 256) return 0;
    *(uint64_t*)(phys_to_virt(paging_get_phys(as->pml4_phys, rsp) + (rsp & 0xFFF))) = 0;
    for (int i = envc - 1; i >= 0; i--) {
        rsp -= 8;
        if (rsp < USER_STACK_LIMIT + 256) return 0;
        *(uint64_t*)(phys_to_virt(paging_get_phys(as->pml4_phys, rsp) + (rsp & 0xFFF))) = envp_addrs[i];
    }
    rsp -= 8;
    *(uint64_t*)(phys_to_virt(paging_get_phys(as->pml4_phys, rsp) + (rsp & 0xFFF))) = 0;
    for (int i = argc - 1; i >= 0; i--) {
        rsp -= 8;
        if (rsp < USER_STACK_LIMIT + 256) return 0;
        *(uint64_t*)(phys_to_virt(paging_get_phys(as->pml4_phys, rsp) + (rsp & 0xFFF))) = argv_addrs[i];
    }
    rsp -= 8;
    if (rsp < USER_STACK_LIMIT + 256) return 0;
    *(uint64_t*)(phys_to_virt(paging_get_phys(as->pml4_phys, rsp) + (rsp & 0xFFF))) = (uint64_t)argc;

    return rsp;
}

Process* process_create(int ppid, AddressSpace* as, const char* name) {
    (void)name;

    if (g_next_pid >= PROC_MAX_PID) {
        serial_emergency_print("process: max PID reached\n");
        return NULL;
    }

    Process* p = (Process*)kzalloc(sizeof(Process));
    if (!p) return NULL;

    p->pid = g_next_pid++;
    p->ppid = ppid;
    p->state = PROC_READY;
    p->exit_code = 0;
    p->as = as;

    p->kstack_base = vmalloc_kernel_stack();
    if (!p->kstack_base) {
        kfree(p);
        return NULL;
    }
    p->kstack_top = p->kstack_base + KERNEL_STACK_SIZE;
    p->kstack_guard = p->kstack_base;

    p->uctx = (TaskContext*)kzalloc(sizeof(TaskContext));
    if (!p->uctx) {
        vfree_kernel_stack(p->kstack_base);
        kfree(p);
        return NULL;
    }

    p->preemptible = (as != NULL) ? 1 : 0;
    p->need_resched = 0;
    p->vruntime = 0;
    p->weight = NICE_0_WEIGHT;
    p->last_run = 0;

    p->cwd_cluster = g_fat32_root_cluster;
    strcpy(p->cwd_path, "/");

    p->next = g_processes;
    p->run_next = NULL;
    g_processes = p;

    if (ppid > 0) {
        Process* parent = process_get_by_pid(ppid);
        if (parent && parent->child_count < PROC_MAX_CHILDREN) {
            parent->children[parent->child_count++] = p;
        }
    }

    serial_emergency_print("process: created pid=");
    serial_emergency_print_hex((uint64_t)p->pid);
    serial_emergency_print(" ppid=");
    serial_emergency_print_hex((uint64_t)ppid);
    serial_emergency_print("\n");

    return p;
}

void process_destroy(Process* p) {
    if (!p) return;

    Process** pp = &g_processes;
    while (*pp) {
        if (*pp == p) {
            *pp = p->next;
            break;
        }
        pp = &(*pp)->next;
    }

    if (p->ppid > 0) {
        Process* parent = process_get_by_pid(p->ppid);
        if (parent) {
            for (int i = 0; i < parent->child_count; i++) {
                if (parent->children[i] == p) {
                    for (int j = i; j < parent->child_count - 1; j++) {
                        parent->children[j] = parent->children[j + 1];
                    }
                    parent->child_count--;
                    break;
                }
            }
        }
    }

    if (p->uctx) kfree(p->uctx);
    if (p->kstack_base) vfree_kernel_stack(p->kstack_base);
    if (p->as) vmm_destroy(p->as);

    if (g_current == p) g_current = NULL;

    kfree(p);
}

Process* process_get_by_pid(int pid) {
    for (Process* p = g_processes; p; p = p->next) {
        if (p->pid == pid) return p;
    }
    return NULL;
}

int process_count(void) {
    int n = 0;
    for (Process* p = g_processes; p; p = p->next) n++;
    return n;
}

int sys_getpid_impl(void) {
    return g_current ? g_current->pid : -1;
}

void sys_exit_impl(int code) {
    Process* p = g_current;
    if (!p) return;

    p->exit_code = code;
    p->state = PROC_ZOMBIE;

    serial_emergency_print("process: pid=");
    serial_emergency_print_hex((uint64_t)p->pid);
    serial_emergency_print(" exited with code=");
    serial_emergency_print_hex((uint64_t)code);
    serial_emergency_print("\n");

    scheduler_exit_current();
}

static int do_fork(Process* parent, uint64_t rip, uint64_t rsp, uint64_t rflags,
                   SyscallContext* ctx) {
    if (!parent) return -1;
    if (!parent->as) {
        serial_emergency_print("fork: parent has no AS\n");
        return -1;
    }

    Process* child = process_create(parent->pid, NULL, "child");
    if (!child) return -1;

    child->cwd_cluster = parent->cwd_cluster;
    strcpy(child->cwd_path, parent->cwd_path);

    child->as = vmm_clone(parent->as);
    if (!child->as) {
        process_destroy(child);
        return -1;
    }
    child->preemptible = 1;

    parent->uctx->rip    = rip;
    parent->uctx->rsp    = rsp;
    parent->uctx->rflags = rflags;
    child->uctx->rip     = rip;
    child->uctx->rsp     = rsp;
    child->uctx->rflags  = rflags;
    child->uctx->rax     = 0;
    parent->uctx->rax    = child->pid;

    process_setup_initial_frame(child, rip, rsp, 0x23, 0x1B, 0x202);

    if (ctx) {
        uint64_t* frame = (uint64_t*)child->saved_rsp;
        frame[0]  = ctx->r15;
        frame[1]  = ctx->r14;
        frame[2]  = ctx->r13;
        frame[3]  = ctx->r12;
        frame[4]  = ctx->rflags;
        frame[5]  = ctx->r10;
        frame[6]  = ctx->r9;
        frame[7]  = ctx->r8;
        frame[8]  = ctx->rdi;
        frame[9]  = ctx->rsi;
        frame[10] = ctx->rbp;
        frame[11] = ctx->rdx;
        frame[12] = ctx->rip;
        frame[13] = ctx->rbx;
        frame[14] = 0;
    }
    scheduler_add(child);
    return child->pid;
}

int sys_fork_impl(SyscallContext* ctx) {
    if (!ctx) return -1;
    Process* parent = process_get_current();
    return do_fork(parent, ctx->rip, ctx->rsp, ctx->rflags, ctx);
}

int kernel_fork(void) {
    Process* parent = process_get_current();
    if (!parent || !parent->uctx) return -1;
    return do_fork(parent, parent->uctx->rip, parent->uctx->rsp,
                   parent->uctx->rflags, NULL);
}

int sys_wait_impl(int pid, int* status) {
    Process* parent = process_get_current();
    if (!parent) return -1;

    for (;;) {
        Process* child = NULL;
        for (int i = 0; i < parent->child_count; i++) {
            if (pid == -1 || parent->children[i]->pid == pid) {
                child = parent->children[i];
                break;
            }
        }
        if (!child) return -1;

        if (child->state == PROC_ZOMBIE) {
            if (status) *status = child->exit_code;
            int child_pid = child->pid;
            process_destroy(child);
            return child_pid;
        }

        __asm__ volatile("sti; hlt" ::: "memory");
    }
}

int sys_exec_impl(const char* user_path, char** user_argv, char** user_envp,
                 SyscallContext* ctx) {
    Process* p = process_get_current();
    if (!p) return 1;

    char kpath[256];
    if (copy_from_user_string(kpath, user_path, sizeof(kpath)) != 0) return 2;

    char* arg_storage = (char*)kmalloc(64 * 2 * 256);
    if (!arg_storage) return 5;
    char (*kargv_buf)[256] = (char (*)[256])arg_storage;
    char (*kenvp_buf)[256] = (char (*)[256])(arg_storage + 64 * 256);
    char* kargv[64];
    char* kenvp[64];
    int argc = 0, envc = 0;

    for (int i = 0; i < 64; i++) {
        kargv[i] = NULL;
        kenvp[i] = NULL;
    }

    if (user_argv) {
        for (int i = 0; i < 63; i++) {
            char* ptr;
            if (copy_from_user(&ptr, &user_argv[i], sizeof(ptr)) != 0) break;
            if (!ptr) {
                kargv[i] = NULL;
                break;
            }
            if (copy_from_user_string(kargv_buf[i], ptr, sizeof(kargv_buf[i])) != 0) break;
            kargv[i] = kargv_buf[i];
            argc++;
        }
        kargv[argc] = NULL;
    }
    if (user_envp) {
        for (int i = 0; i < 63; i++) {
            char* ptr;
            if (copy_from_user(&ptr, &user_envp[i], sizeof(ptr)) != 0) break;
            if (!ptr) {
                kenvp[i] = NULL;
                break;
            }
            if (copy_from_user_string(kenvp_buf[i], ptr, sizeof(kenvp_buf[i])) != 0) break;
            kenvp[i] = kenvp_buf[i];
            envc++;
        }
        kenvp[envc] = NULL;
    }

    File* f = NULL;
    if (fs_open(kpath, FS_READ, &f) != 0) {
        kfree(arg_storage);
        return 3;
    }

    uint64_t fsize = f->size;
    if (fsize == 0 || fsize > 16 * 1024 * 1024) {
        fs_close(f);
        kfree(arg_storage);
        return 4;
    }

    uint8_t* elf_buf = (uint8_t*)kmalloc(fsize);
    if (!elf_buf) {
        fs_close(f);
        kfree(arg_storage);
        return 5;
    }
    int64_t n = fs_read(f, elf_buf, fsize);
    fs_close(f);
    if (n != (int64_t)fsize) {
        kfree(elf_buf);
        kfree(arg_storage);
        return 6;
    }

    AddressSpace* new_as = vmm_create();
    if (!new_as) {
        kfree(elf_buf);
        kfree(arg_storage);
        return 7;
    }
    if (vmm_alloc_user_stack(new_as) != 0) {
        vmm_destroy(new_as);
        kfree(elf_buf);
        kfree(arg_storage);
        return 9;
    }
    if (vmm_map_user_abi(new_as) != 0) {
        vmm_destroy(new_as);
        kfree(elf_buf);
        kfree(arg_storage);
        return 10;
    }

    ElfLoaded el;
    if (elf_load(elf_buf, fsize, new_as, &el) != 0) {
        vmm_destroy(new_as);
        kfree(elf_buf);
        kfree(arg_storage);
        return 11;
    }
    kfree(elf_buf);

    uint64_t entry_phys = paging_get_phys(new_as->pml4_phys, el.entry);
    if (!entry_phys || !paging_user_access(new_as->pml4_phys, el.entry, 0)) {
        serial_emergency_print("exec: entrypoint is not user mapped: ");
        serial_emergency_print_hex(el.entry);
        serial_emergency_print(" phys=");
        serial_emergency_print_hex(entry_phys);
        serial_emergency_print("\n");
        vmm_destroy(new_as);
        return 14;
    }

    uint64_t new_rsp = build_user_stack(new_as, kargv, kenvp);
    kfree(arg_storage);
    if (!new_rsp) {
        vmm_destroy(new_as);
        return 12;
    }

    AddressSpace* old_as = p->as;
    new_as->kstack_top = p->kstack_top;
    new_as->kstack_phys = 0;
    uint64_t kernel_rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(kernel_rsp));
    if (!paging_get_phys(new_as->pml4_phys, kernel_rsp)) {
        serial_emergency_print("exec: kernel stack absent in new AS, rsp=");
        serial_emergency_print_hex(kernel_rsp);
        serial_emergency_print(" pml4=");
        serial_emergency_print_hex(new_as->pml4_phys);
        serial_emergency_print("\n");
        vmm_destroy(new_as);
        return 13;
    }
    p->as = new_as;
    paging_switch(new_as->pml4_phys);
    if (old_as) vmm_destroy(old_as);

    for (int i = 0; i < PROC_MAX_FD; i++) {
        if (p->fds[i]) {
            fs_close(p->fds[i]);
            p->fds[i] = NULL;
        }
    }

    p->uctx->rip    = el.entry;
    p->uctx->rsp    = new_rsp;
    p->uctx->rflags = 0x202;
    p->uctx->rax    = 0;

    if (ctx) {
        ctx->rip    = el.entry;
        ctx->rsp    = new_rsp;
        ctx->rflags = 0x202;
        ctx->rax    = 0;
    }

    tss_set_rsp0(p->kstack_top);
    percpu_get(0)->kstack_top = p->kstack_top;

    serial_emergency_print("exec: ");
    serial_emergency_print(kpath);
    serial_emergency_print(" entry=");
    serial_emergency_print_hex(el.entry);
    serial_emergency_print(" rsp=");
    serial_emergency_print_hex(new_rsp);
    serial_emergency_print(" argc=");
    serial_emergency_print_hex(argc);
    serial_emergency_print("\n");

    return 0;
}