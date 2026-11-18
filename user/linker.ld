/* user/linker.ld — линкер-скрипт для user-программ LiveOS */

ENTRY(_start)

SECTIONS {
    . = 0x400000;

    .text : ALIGN(4096) {
        *(.text.start)
        *(.text*)
    }

    .rodata : ALIGN(4096) {
        *(.rodata*)
    }

    .data : ALIGN(4096) {
        *(.data*)
    }

    .bss : ALIGN(4096) {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    .exp : ALIGN(8) {
        __exp_start = .;
        KEEP(*(.exp))
        __exp_end = .;
    }

    .imp : ALIGN(8) {
        __imp_start = .;
        KEEP(*(.imp))
        __imp_end = .;
    }

    /DISCARD/ : {
        *(.note*)
        *(.comment)
        *(.eh_frame*)
        *(.interp)
        *(.dynamic)
        *(.dynsym)
        *(.dynstr)
        *(.hash)
        *(.gnu.hash)
        *(.plt)
        *(.plt.got)
        *(.rela*)
    }
}