.section .text

.global vmm_enter_user_asm
vmm_enter_user_asm:
    movq %rdi, g_current_as(%rip)
    movq %rsp, g_kernel_rsp(%rip)
    
    leaq 1f(%rip), %rax
    movq %rax, g_kernel_rip(%rip)

    pushq $0x1B
    pushq %rdx
    pushq $0x202
    pushq $0x23
    pushq %rsi
    swapgs
    iretq

1:
    xorl %eax, %eax
    ret

.global vmm_yield_asm
vmm_yield_asm:
    movq %rdi, g_user_rip(%rip)
    movq %rsi, g_user_rsp(%rip)
    movq %rdx, g_user_rflags(%rip)

    movq g_kernel_rsp(%rip), %rsp

    pushq $0x10
    pushq g_kernel_rsp(%rip)
    pushq $0x202
    pushq $0x08
    pushq g_kernel_rip(%rip)
    iretq

.section .bss
.align 8
g_current_as:      .skip 8
g_kernel_rsp:      .skip 8
g_kernel_rip:      .skip 8
g_user_rip:        .skip 8
g_user_rsp:        .skip 8
g_user_rflags:     .skip 8

.section .note.GNU-stack,"",@progbits