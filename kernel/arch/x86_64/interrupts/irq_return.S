.section .text
.global irq_return

irq_return:
    movq %rdi, %rsp

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
    jz 1f
    swapgs
1:
    iretq

.section .note.GNU-stack,"",@progbits