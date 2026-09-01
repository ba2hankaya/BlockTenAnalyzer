ENTRY(_start)

MEMORY {
    /* QEMU virt machine RAM starts at 0x80000000 */
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 3K
}

SECTIONS {
    /* Boot section must be first */
    .text : {
        *(.text.boot)
        *(.text .text.*)
    } > RAM

    .rodata : {
        *(.rodata .rodata.*)
    } > RAM

    .data : {
        *(.data .data.*)
    } > RAM

    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } > RAM

    /* Allocate 1KB for the stack */
    .stack : {
      . = ALIGN(16);
      _stack_bottom = .;
      . += 0x400;
      _stack_top = .;
    } > RAM
}
