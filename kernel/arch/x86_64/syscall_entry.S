.section .text
.extern syscall_handler
.global syscall_entry

syscall_entry:
    swapgs

    movq %rsp, %gs:24

    movq %gs:16, %rsp

    pushq %gs:24
    pushq %r11
    pushq %rcx

    pushq %rbx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    pushq %rax

    movq %rsp, %rdi

    incq %gs:48

    call syscall_handler

    movq %rax, (%rsp)

    popq %rax
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rbx

    popq %rcx
    popq %r11
    popq %rsp

    swapgs

    sysretq

.section .note.GNU-stack,"",@progbits