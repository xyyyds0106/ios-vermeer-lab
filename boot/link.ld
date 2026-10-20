ENTRY(_head)
SECTIONS {
    . = 0;
    .text : { *(.text.head) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    . = ALIGN(16);
    _bss_start = .;
    .bss : { *(.bss*) *(COMMON) }
    . = ALIGN(16);
    _bss_end = .;
    . += 0x8000;
    stack_top = .;
    _end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
