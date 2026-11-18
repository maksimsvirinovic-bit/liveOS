.section .text.start
.global _start
.extern kmain

_start:
    cli
    movabsq $__boot_stack_top_virt, %rsp
    xorq %rbp, %rbp

    movabsq $__bss_start, %rdi
    movabsq $__bss_end, %rcx
    subq %rdi, %rcx
    xorl %eax, %eax
    rep stosb

    movabsq $__init_array_start, %rdi
    movabsq $__init_array_end, %rsi
    call run_global_ctors

    movabsq $kmain, %rax
    call *%rax

9:  cli
    hlt
    jmp 9b

.section .bss
.align 16
.global __boot_stack_bottom_virt
__boot_stack_bottom_virt:
    .skip 16384
.global __boot_stack_top_virt
__boot_stack_top_virt:

.section .note.GNU-stack,"",@progbits