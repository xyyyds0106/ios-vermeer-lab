.section .text.head, "ax"
.global _head
_head:
    b start
    .long 0
    .quad 0
    .quad _end
    .quad 0xa
    .quad 0, 0, 0
    .ascii "ARM\x64"
    .long 0
.text
start:
    msr daifset, #15
    mov x19, x0
    adr x1, stack_top
    mov sp, x1
    adr x1, vectors
    msr vbar_el1, x1
    isb
    adr x1, _bss_start
    adr x2, _bss_end
2:  cmp x1, x2
    b.hs 3f
    str xzr, [x1], #8
    b 2b
3:
    mov x0, x19
    bl boot_main
1:  wfe
    b 1b
