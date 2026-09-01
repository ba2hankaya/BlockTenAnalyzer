.global _start

.section .text.boot
_start:
    /* Disable interrupts */
    csrw mie, zero

    /* Set up the stack pointer. 
       _stack_top is defined in the linker script. */
    la sp, _stack_top

    /* Jump to C main */
    call main

    /* Infinite loop if main ever returns */
1:  j 1b
