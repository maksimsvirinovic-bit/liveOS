#ifndef PROCESS_H
#define PROCESS_H

#include "fs.h"
#include <stdint.h>
#include <limits.h>
#include <mm/vmm.h>

#define PROC_MAX_PID       INT_MAX
#define PROC_MAX_CHILDREN  64
#define PROC_MAX_FD        2048
#define USER_STACK_TOP     0x0000000001000000ULL
#define PROC_MAX_PRIORITY  32
#define NICE_0_WEIGHT      2048

typedef enum {
    PROC_RUNNING = 0,
    PROC_READY,
    PROC_SLEEPING,
    PROC_ZOMBIE,
} ProcessState;

typedef struct TaskContext {
    uint64_t rip;
    uint64_t rsp;
    uint64_t rflags;
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
} TaskContext;

typedef struct SavedContext {
    uint64_t rsp;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
} SavedContext;

typedef struct Process {
    int pid;
    int ppid;
    ProcessState state;
    int exit_code;

    AddressSpace* as;

    uint64_t kstack_base;
    uint64_t kstack_top;
    uint64_t kstack_guard;

    TaskContext* uctx;

    uint64_t saved_rsp;

    uint64_t vruntime;
    int weight;
    uint64_t last_run;
    int preemptible;
    int need_resched;

    File* fds[PROC_MAX_FD];
    int   fd_count;

    int      wait_pid;
    int*     wait_status_user;
    int      wait_done;
    int      wait_result_pid;
    int      wait_result_status;

    uint32_t cwd_cluster;
    char     cwd_path[256];

    struct Process* children[PROC_MAX_CHILDREN];
    int child_count;

    struct Process* next;
    struct Process* run_next;
} Process;

int       copy_from_user_string(char* dst, const char* user_src, uint64_t max_size);
void      process_init(void);
Process*  process_get_current(void);
void      process_set_current(Process* p);
Process*  process_create(int ppid, AddressSpace* as, const char* name);
void      process_destroy(Process* p);
Process*  process_get_by_pid(int pid);
int       process_count(void);
void      process_setup_initial_frame(Process* p, uint64_t rip, uint64_t rsp,
                                  uint64_t cs, uint64_t ss, uint64_t rflags);
int       sys_getpid_impl(void);
void      sys_exit_impl(int code);
int       sys_fork_impl(SyscallContext* ctx);
int       kernel_fork(void);
int       sys_wait_impl(int pid, int* status);
int       sys_exec_impl(const char* user_path, char** user_argv, char** user_envp,
                      SyscallContext* ctx);

#endif