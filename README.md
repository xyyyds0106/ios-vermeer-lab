# vermeer XNU bring-up lab

Private experiment for Redmi K70 (vermeer / SM8550).

The initial workflow reproduces the **SWEET reference build** from
https://github.com/dipa-studio/ios-sweet at
11da58bd050672960c0a83b5bef2176047eea479. It is **not a K70 kernel** and must
not be booted or flashed on vermeer. The patch authors retain their copyright
and the original source and dependency licensing applies.

Confirmed on one K70 through custom temporary probes: linear framebuffer
output, EL1 execution, 19.2 MHz counter, 16KB MMU and caches, virtual timer IRQs,
and sparse access to the selected 256MB RAM window. A photograph of XNU
attempt 002 confirms early kernel execution and a subsequent console-mapping
panic. No root filesystem, userspace or iOS interface has run. Secondary cores
and storage drivers remain unverified.

The workflow is manual-only, has read-only repository permissions and a
45-minute job limit. It does not contain Apple firmware or device dumps.

The `vermeer-early` profile uses reference patches 0001..0018 plus a targeted
early I/O page-table budget patch. It adapts the early
console to 0xb8000000 / 1440x3200 / stride 5760, and the virtual timer default
to INTID 27. It intentionally excludes the later storage and peripheral drivers.
It still uses the reference machine configuration name SWEET internally. A
successful build is not proof it boots: vermeer ADT, validated RAM allocation
and a v4 boot shim must be prepared before any device test.

First device attempt stopped in the loader with `no payload in initrd`.
The observed initrd length equals aligned vendor ramdisk + stock init_boot
ramdisk: ABL did not pass the incoming boot ramdisk to this test image.
The loader now embeds its XNU payload inside the ARM64 Image and uses linker
symbols to locate it. The failed attempt did not enter XNU.

Second attempt entered XNU and reached `SWEET EARLY 20: kprintf startup
complete`, then panicked in `pmap_map_bd` while mapping the generic video
console. The profile now budgets early I/O page tables for both framebuffer
console mappings; the serial ADT maps only the active pixel bytes. No checks
are bypassed; missing-PTE diagnostics include the failing addresses.
