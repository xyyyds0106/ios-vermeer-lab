/* Source-only PID1 checks. Android baseline CRCs, no partition contents.
 * open(FWRITE) must fail EROFS. Only read/open/lseek/close syscalls follow.
 * Test an 8KiB read at offset8192 and a 512-byte read at offset8704.
 * The first two userdata blocks contain mount-varying metadata and are not
 * stable Android CRC baselines. These tests use the separate captured range.
 */
#define SYS_read 3
#define SYS_close 6
#define SYS_lseek 199

    .p2align 2
block_test:
    sub sp, sp, #32
    stp x29, x30, [sp]
    stp x20, x21, [sp, #16]
    sub sp, sp, #8192
    adr x0, block_path
    mov x1, #O_RDWR
    mov x2, #0
    mov x16, #SYS_open
    svc #0x80
    b.cc block_writable
    cmp x0, #30                 /* Darwin EROFS */
    b.ne block_fail
    adr x1, readonly_ok
    adr x2, readonly_end
    sub x2, x2, x1
    bl write_console
    adr x0, block_path
    mov x1, #0                 /* O_RDONLY */
    mov x2, #0
    mov x16, #SYS_open
    svc #0x80
    b.cs block_fail
    mov x20, x0
    /* Invalid destination must be EFAULT, not a kernel data abort. */
    mov x1, #0
    mov x2, #512
    mov x16, #SYS_read
    svc #0x80
    b.cc block_read_fail
    cmp x0, #14                 /* Darwin EFAULT */
    b.ne block_read_fail
    adr x1, fault_ok
    adr x2, fault_end
    sub x2, x2, x1
    bl write_console
    mov x0, x20
    mov x1, #8192
    mov x2, #0
    mov x16, #SYS_lseek
    svc #0x80
    b.cs block_read_fail
    cmp x0, #8192
    b.ne block_read_fail
    mov x0, x20
    mov x1, sp
    mov x2, #8192
    mov x16, #SYS_read
    svc #0x80
    b.cs block_read_fail
    cmp x0, #8192
    b.ne block_read_fail
    mov x0, sp
    mov x1, #8192
    bl block_crc
    mov w9, #0xbb7c
    movk w9, #0x0d4c, lsl #16
    cmp w0, w9
    b.ne block_read_fail
    adr x1, full_ok
    adr x2, full_end
    sub x2, x2, x1
    bl write_console
    mov x0, x20
    mov x1, #8704
    mov x2, #0                 /* SEEK_SET */
    mov x16, #SYS_lseek
    svc #0x80
    b.cs block_read_fail
    mov x9, #8704
    cmp x0, x9
    b.ne block_read_fail
    mov x0, x20
    mov x1, sp
    mov x2, #512
    mov x16, #SYS_read
    svc #0x80
    b.cs block_read_fail
    cmp x0, #512
    b.ne block_read_fail
    mov x0, sp
    mov x1, #512
    bl block_crc
    mov w9, #0xd6b0
    movk w9, #0x3261, lsl #16
    cmp w0, w9
    b.ne block_read_fail
    mov x0, x20
    mov x16, #SYS_close
    svc #0x80
    adr x1, partial_ok
    adr x2, partial_end
    sub x2, x2, x1
    bl write_console
    b block_done
block_writable:
    mov x16, #SYS_close
    svc #0x80                  /* unexpected open; still never issue a write */
    b block_fail
block_read_fail:
    mov x0, x20
    mov x16, #SYS_close
    svc #0x80
block_fail:
    adr x1, block_bad
    adr x2, block_bad_end
    sub x2, x2, x1
    bl write_console
block_done:
    add sp, sp, #8192
    ldp x29, x30, [sp]
    ldp x20, x21, [sp, #16]
    add sp, sp, #32
    ret

/* IEEE CRC32; input x0 pointer, x1 bytes, output w0. */
block_crc:
    mov w2, #-1
    mov w5, #0x8320
    movk w5, #0xedb8, lsl #16
1:
    ldrb w3, [x0], #1
    eor w2, w2, w3
    mov w4, #8
2:
    and w3, w2, #1
    neg w3, w3
    and w3, w3, w5
    lsr w2, w2, #1
    eor w2, w2, w3
    subs w4, w4, #1
    b.ne 2b
    subs x1, x1, #1
    b.ne 1b
    mvn w0, w2
    ret

block_path: .asciz "/dev/rvermeerufs0"
readonly_ok: .ascii "VERMEER BLOCK: FWRITE open refused EROFS: PASS\n"
readonly_end:
fault_ok: .ascii "VERMEER BLOCK: invalid user pointer refused EFAULT: PASS\n"
fault_end:
full_ok: .ascii "VERMEER BLOCK: offset8192 length8192 CRC matches Android: PASS\n"
full_end:
partial_ok: .ascii "VERMEER BLOCK: offset8704 length512 CRC matches Android: PASS\n"
partial_end:
block_bad: .ascii "VERMEER BLOCK: USERSPACE READ TEST FAILED; RAM init remains alive\n"
block_bad_end:
