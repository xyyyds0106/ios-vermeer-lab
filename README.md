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
early I/O page-table budget and boot-CPU matching patches. It adapts the early
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

Third attempt passed that panic and printed Darwin, VM, scheduler and security
initialization logs, stopping after platform-expert registration. The ADT's
boot CPU is correctly `arm,cortex-a510`, but the reference CPU service matched
only `qcom,kryo470`. The vermeer profile now matches the actual Cortex-A510 node
and keeps the reference single-core CPU registration and SGI self-test.

Fourth attempt confirms `boot CPU 0 registered, SGI1 verified, initialization
complete` and BSD initialization. The `vermeer-ramroot` profile adds reference
patches through 0025 (libpthread, portable crypto, mockfs memory root and the
no-entitlements AMFI provider). It builds a source-only static PID 1 test
program, served as `/sbin/launchd` by mockfs. This test uses RAM only and contains
no Apple userspace firmware, no UFS driver and no iOS interface.

RAM-root attempt 001 is confirmed on hardware by a photograph taken after a
longer observation: `BSD root: md0`, `init running as PID 1`, and many repeated
`VERMEER init: still alive` messages. Keep this version as the verified
checkpoint: commit 2e72c40, Actions run 39428900143. The kernel and init artifact
manifests are checked before packaging. Wait at least 120-150 seconds when
testing because RTC/calendar service waits can leave the early log unchanged.
This proves source-built test userspace runs; it does not prove iOS boots.

Standalone K70 probe010 has independently verified UFS reads: activate device
WLUNd0, query LUN0 capacity, read LBA0..1, and compare all8192bytes with Android.
Zero byte differences, CRC32=45fbee46, block size4096 and last LBA03afa3ff were
confirmed in the user photograph. No disk writes or partition erases occurred.
Probes/photos/device baselines remain private and outside this repository.

The `vermeer-ufs-probe` profile adds an opt-in kernel read self-test to the
verified RAM-root profile. Boot with `vermeer_ufs_probe=1 rd=md0`. It maps the
known UFS/SMMU registers, requires the verified firmware route and disabled
SMMU context, allocates contiguous permanent DMA RAM inside c0000000-d0000000,
verifies every allocated physical page and explicitly synchronizes caches.
It uses only TUR, capacity/read and a device-WLUN Active power transition when
needed, then checks the read CRC against the standalone Android baseline.
It restores the firmware transfer list when idle and continues mockfs PID1.
It exposes no storage device and contains no disk write/erase/format command.
The kernel self-test is now hardware-verified: build39437189570, commit e884ca2,
image SHA256 a6fd60dcd9925a2ffed326a3ab2c5a69f9918936faf501b9e7629fb22e22a838.
The photograph shows allocated DMA PA c2174000, successful Active/TUR/capacity/
8192-byte read, matching CRC45fbee46, result0, firmware queue restored, then
md0 root and continuing PID1 heartbeats. This verifies kernel-mode read DMA
with MMU/cache enabled and preserves the RAM-root test. No filesystem on UFS
has been mounted, and Apple userspace/iOS remain unverified.
