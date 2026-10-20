/* ABL on this device supplies the stock init_boot ramdisk even for fastboot
 * boot. Carry the XNU payload inside the ARM64 Image instead. Keep this after
 * the stack in the linker script so entry.S ADR instructions remain in range.
 */
.section .payload, "a"
.balign 16
.global embedded_payload_begin
.global embedded_payload_end
embedded_payload_begin:
.incbin "out/payload.bin"
embedded_payload_end:
