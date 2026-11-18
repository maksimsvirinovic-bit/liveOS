ENTRY(_start)

SECTIONS {
    . = 0xFFFFFFFF80000000;
    __image_base = .;

    .text : ALIGN(4096) {
        *(.text.start)
        *(.text*)
    }
    .rodata : ALIGN(8) { *(.rodata*) }
    .data : ALIGN(8) { *(.data*) }
    .bss : ALIGN(4096) {
        __bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }
    .init_array : ALIGN(8) {
        __init_array_start = .;
        KEEP(*(.init_array))
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        __init_array_end = .;
    }
    .fini_array : ALIGN(8) {
        __fini_array_start = .;
        KEEP(*(.fini_array))
        __fini_array_end = .;
    }
    __kernel_end = .;

    /DISCARD/ : {
        *(.note.GNU-stack)
        *(.note.gnu.property)
        *(.comment)
        *(.eh_frame)
    }
}