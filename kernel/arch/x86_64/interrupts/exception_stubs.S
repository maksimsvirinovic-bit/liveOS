.section .text
.extern exception_handler

.macro EXC_NO_ERROR num
.global isr\num
isr\num:
    pushq $0
    pushq $\num
    jmp exception_common
.endm

.macro EXC_ERROR num
.global isr\num
isr\num:
    pushq $\num
    jmp exception_common
.endm

exception_common:
    testb $3, 24(%rsp)
    jz 1f
    swapgs
1:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rdi

    call exception_handler

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    addq $16, %rsp

    testb $3, 8(%rsp)
    jz 2f
    swapgs
2:
    iretq

EXC_NO_ERROR 0
EXC_NO_ERROR 1
EXC_NO_ERROR 2
EXC_NO_ERROR 3
EXC_NO_ERROR 4
EXC_NO_ERROR 5
EXC_NO_ERROR 6
EXC_NO_ERROR 7
EXC_ERROR    8
EXC_NO_ERROR 9
EXC_ERROR    10
EXC_ERROR    11
EXC_ERROR    12
EXC_ERROR    13
EXC_ERROR    14
EXC_NO_ERROR 15
EXC_NO_ERROR 16
EXC_ERROR    17
EXC_NO_ERROR 18
EXC_NO_ERROR 19
EXC_ERROR    20
EXC_ERROR    21
EXC_NO_ERROR 22
EXC_NO_ERROR 23
EXC_NO_ERROR 24
EXC_NO_ERROR 25
EXC_NO_ERROR 26
EXC_NO_ERROR 27
EXC_NO_ERROR 28
EXC_ERROR    29
EXC_ERROR    30
EXC_NO_ERROR 31

.section .note.GNU-stack,"",@progbits