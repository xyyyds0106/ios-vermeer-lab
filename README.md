# vermeer XNU bring-up lab

Experimental bring-up for Redmi K70 (vermeer / SM8550).

GUI008 (`fd9878b`, Actions39554200501) temporarily boots the retained root;
the user confirms reaching Hello/the setup interface. The clean-root migration
baseline returns success and repeated snapshots show SpringBoard entering
UIApplicationMain's event loop. Touch does not work reliably: the user reports
the picture stops changing after a finger swipe. USB ping and queued snapshots
still work in that state. This is a visible setup milestone; hardware touch,
stable transitions and Adreno/Metal acceleration are not yet demonstrated.

The GUI009 candidate fixes a display-buffer lifetime gap: pending swaps retain
their surfaces until the delayed panel copy, including when userspace releases
them or a newer frame supersedes them. Signed scrolling rectangles are clipped
against both source storage and the panel without resetting unrelated axes.
Host tests exercise release-before-present, replacement during a copy and
200,000 clipped-copy cases. Read-only `frame`/`input` counters distinguish
submission, panel copy and HID dispatch. These changes do not register a GPU or
a physical touch controller. Local affected-source compiles and host checks
pass; the complete build and real-device result are pending.

GUI007 (`a6a6ddb`, Actions39549812373) boots with continued display swaps and
passes read-only USB manual sampling, image maps and post-sample liveness.
The same-kernel007i variant adds installer services to the selected task list.
Real stacks identify SystemAppMigrator waiting for app-store/installation
completion while appstored also waits in DataMigration. The reference root
tweaks document this clean-root upgrade-migration delay as well.

The opt-in repair, `vermeer_migration_baseline=1`, initializes only the
20H392 migration-version preference on the disposable root. It stages/syncs a
complete plist, preserves an existing preference inode in an exclusive hidden
backup, publishes with an exclusive rename, and records a one-time marker.
Existing backup/ambiguous state is refused; optional preparation failure leaves
the normal migration boot and USB available. Host failure injection covers
every transaction step, interrupted recovery and repeated invocation. Root
UUID/extent checks remain prerequisites; the actual SystemVersion must match
16.7.16/20H392. This is a laboratory bootstrap, not evidence that Apple's
migration plugins completed. Setup and activation records are not modified.
The full build and application of the baseline have passed on the test handset.

GUI006s reuses the verified GUI006 kernel with selected-process sampling.
Repeated symbolicated snapshots show SpringBoard waiting for the system-shell
check-in, backboardd waiting for DataMigration, and the migration daemon waiting
for its plugin wrapper. The wrapper waits inside a dynamically loaded module;
its exact path is not yet captured. GUI007 adds bounded dyld image maps and a
USB `sample` request on the existing worker, preserving graphics and storage
behavior. Local affected-source compiles and ASan/UBSan boundary checks pass;
the full build and new hardware result remain pending. No root reflash is needed.

Integrated GUI006 (`c6fadce`, Actions39544696722) temporarily boots on the K70
and passes first presentation: backboardd alternates two 1125x2436 BGRA
surfaces on the guarded 1440x3200 panel, with more than 8,000 submitted swaps
and continued USB logs in the initial four-minute observation. SpringBoard
and backboardd each start once with no captured crash; CommCenter still
crashes. The user confirms an Apple logo and progress bar on the panel;
Setup/Hello and the home screen have not yet been observed. USB close/reopen,
ping/help/status and retained-log replay pass after more than six minutes.
Physical touch input is not implemented. The existing UFS root was retained;
no partition was flashed for this test.

Earlier integrated GUI005 (`bc2a4bc`, Actions39542363383) temporarily boots on the K70.
The framebuffer geometry guard passes, and backboardd opens the display,
IOSurface and HID services and creates a 1440x3200 BGRA surface. SpringBoard
and backboardd each start once without a captured crash, but logs then stop
before a confirmed frame presentation. No iOS GUI was proven. The GUI006 delta
avoids switching the read-only-late console selector on the first frame,
preserves USB logging and panic text, uses a centered 1125x2436 UIKit canvas,
and adds work-interval and cached-IOSurface lookup compatibility.

Earlier integrated003 evidence: official launchd passes FIPS/keybag boot
tasks and repeatedly starts SpringBoard/backboardd and multiple Apple daemons.
Those services crashed and restarted. Integrated004
adds an opt-in [USB serial debug console](USB-DEBUG.md) for log capture and
bounded read-only queries. Its temporary boot (source `f0aec9c`,
Actions39540409885) is hardware-confirmed: Windows COM6 receives the early
console and live daemon crash backtraces, and `status` returns counters.

The initial workflow reproduces the **SWEET reference build** from
https://github.com/dipa-studio/ios-sweet at
11da58bd050672960c0a83b5bef2176047eea479. It is **not a K70 kernel** and must
not be booted or flashed on vermeer. The patch authors retain their copyright
and the original source and dependency licensing applies.

Confirmed on one K70 through custom temporary probes: linear framebuffer
output, EL1 execution, 19.2 MHz counter, 16KB MMU and caches, virtual timer IRQs,
and sparse access to the selected 256MB RAM window. XNU now boots a source-built
PID1 from a journaled HFSX RAM root and independently verifies raw and VFS block
UFS reads against Android's CRC baseline. The first integrated boot has now
mounted the prepared HFS root on UFS and entered Apple's launchd/libignition,
then stopped when the dyld shared cache was rejected by filesystem policy.
Secondary cores, sustained Apple service startup and an iOS interface remain
unverified. The following entries record
the individual milestones, including failures that have since been resolved.

Latest integrated001 photo: `VermeerIOSRoot002` mounts, `/sbin/launchd` enters
libignition, then shared-cache mapping reports `vnode is not SIP-protected`.
PID1 exits because `libSystem.B.dylib` cannot be found outside that cache, and
XNU panics. This is an Apple userspace entry result, not a working iOS session.
The next integrated image adds only `CSR_ALLOW_UNRESTRICTED_FS` in the iOS ADT
(`lp-sip0` u64=2), plus the unsealed-root flag (`lp-sip1` u32=1), using XNU's
existing local-policy path. Ownership, code-signature and cache-directory checks
remain in place. Diagnostic ADTs have no policy change. The adapter also handles
HFS's structured `DKIOCSYNCHRONIZE` via the existing full SCSI cache flush;
the previous legacy-only ioctl caused journal flush errors `0x19` (ENOTTY).
The prepared root is already on the phone; the repair needs a temporary kernel
boot, not another root flash. These repairs are not yet hardware-verified.

Integrated002 source `77fb6ae`, Actions39533922823 succeeded. Kernel SHA256
`11dcc7a343fa742eda9f361093e500fe4c63f495da05691b2f2b89239d988bfb`;
boot-image SHA256
`5ffd63b2f60ef233841b690704138a77b266b781b6f603b2bb2ee41468e65b70`.
Local integration compile, serialized policy types/mask and diagnostic isolation,
and ASan/UBSan GPT/DMA/media-boundary checks passed. The repaired image was
accepted by fastboot as a temporary boot; its screen result is pending. Existing
root002 was retained and no partition was flashed again.

Integrated002 photograph confirms the shared-cache wall is passed: launchd
executes mount/restore/FIPS boot tasks, and the displayed corecrypto tests pass.
It then reports `failed to load system bag`, requests recovery reboot, unmounts
the HFS root and stops in `Halt/Restart Timed Out`. The old journal flush ENOTTY
messages are absent from this shutdown. This does not yet prove sustained
system-service startup or an interface.

The actual CI log/tree revealed that0038 failed atomically after the earlier
K70 CPU-personality edit changed its context. The external loader treated this
as a warning and also skipped dependent0040/0043/0044/0045. Thus the prior local
fuzzy-patch check and green compile did not establish those services existed.
The new profile adapts the later context to `arm,cortex-a510`; every numbered
project patch must now apply or the build stops. A final-kernel check also
requires AKS/watchdog/crash-reason code. Strict `git apply` of all previously
missing patches on the actual002 tree passes, and the artifact check correctly
rejects the incomplete002 kernel.

The next integration also encodes `no-effaceable-storage` as a u32 boolean and
sets the SHA384 cryptex algorithm. An explicit `vermeer_prepare_root=1` boot
option creates only missing first-boot test-keybag metadata before Apple init,
through normal VFS calls on the UUID/geometry-guarded writable HFS root. Existing
bags are never replaced. The data is an explicit lab marker for the passcode-free
test AppleKeyStore, not an encrypted bag or a hardware security implementation.
Six affected integration/IOKit source files pass local cross-compilation.

Integrated003 source `2206cc7`, Actions39536101467 succeeded, including the
actual-kernel service check. The real CI log confirms all five formerly skipped
patches applied and zero numbered project patches skipped. Kernel SHA256
`8b3053708227d72b436be9101b077acc3ca03904749ef95ef9885df4be63b171`;
boot SHA256 `72bdc02136cd487b0db7f714f72aedc796e9f07108b16b8f4fd52e561d28f587`.
Packaging verifies the new metadata properties and explicit preparation opt-in.
Fastboot accepted the temporary boot; hardware keybag/startup results remain
pending. The existing root002 volume was retained without another partition flash.

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

The `vermeer-hfs-ramroot` profile adds reference patches0026/0027 and the pinned
Apple OSS HFS staging script. It retains the verified opt-in UFS self-test and
builds a source PID1 labeled `VERMEER HFS userspace`. Package that executable
inside a small HFSX RAM disk, not as a raw mockfs executable. The test is meant
to verify actual filesystem mounting and reading while leaving phone storage
untouched. A hardware photograph now confirms `hfs: mounted VermeerHFSTest`,
`VERMEER HFS userspace: init running as PID 1` and continuing heartbeats, after
the retained UFS CRC self-test passes and restores the firmware queue.
Checkpoint: commit43261fa, Actions run39439982542, image SHA256
125d3b232f6411461cff5628522b49ff4e94adaab807cd323e21a5116715facf.
This is a real HFSX RAM filesystem, not Apple launchd or an iOS interface.

The `vermeer-ufs-block` profile retains HFSX RAM root and adds an opt-in
read-only BSD adapter (`vermeer_ufs_block=1 rd=md0`). It first runs the verified
LUN0 CRC test, validates the primary GPT header/entry CRCs and all partition
ranges/overlaps, then requires userdata's independent Android start/size.
Only that partition is exposed at `/dev/{r,}vermeerufs0`. Transport reads use
the existing guarded, cache-synchronized 8KiB DMA buffer. Any transfer error
disables further reads; pending DMA is never retried or freed. Writes are
refused at open/strategy/character-I/O boundaries and no disk-write opcode is
available. Source PID1 tests FWRITE rejection and two CRC-checked raw reads
(8192 bytes from offset8192, 512 bytes from offset8704), retaining heartbeat output.
Host tests exercise the shared GPT and offset parser with valid/corrupt tables,
duplicate/overlapping partitions, truncated formats and arithmetic boundaries.
Build39443672315 at7bdcb7d succeeded. The kernel/init manifests were verified;
the new HFSX RAM disk passed offline fsck. The temporary device test is awaiting
screen evidence; it does not mount the Android filesystem or select UFS as the
boot root. Build success and bootloader acceptance are not read-test success.

The first block-adapter photograph confirms GPT validation, device registration,
HFSX RAM root/PID1 and FWRITE rejection, followed by a data abort in memmove
(pc fffffe0007b102d8, lr vb_strategy+0x210). The raw physio buffer was a user
address: buf_map does not convert non-cluster B_PHYS buffers into kernel maps.
The adapter now uses uiomove for character reads and refuses B_PHYS strategy
buffers before mapping/copying them. Ordinary kernel/UPL block buffers retain
the strategy path. PID1 additionally checks that a NULL user destination yields
EFAULT before attempting valid CRC reads. Fix build39446089277 atd784482 passed;
the temporary hardware retest is pending. The initial offset0 Android baseline
changed by32 bytes after Android remounting. The replacement range atoffset8192
was independently captured and matched byte-for-byte across an Android reboot;
it avoids those changing blocks. The test init can also be assembled locally
with LLVM's Mach-O assembler and the existing relocation-rejecting mkinit tool.

The corrected raw-user-read path is now hardware-verified by a photograph:
FWRITE rejection, NULL-destination EFAULT and both Android CRC reads all PASS,
with many continuing PID1 heartbeats. Checkpoint: kernel build39446089277 at
d784482, test-init source9d79fae, boot image003 SHA256
c58dbc176a6557e068d989170ab080d931b23438467972787778d27390a79ab8.
Keep this image as the regression version. The BSD block-device/VFS path and
an actual filesystem mount on UFS still need separate validation; this is
HFSX RAM root plus raw UFS reads, with no Apple userspace or iOS interface.

The next source-init test additionally opens `/dev/vermeerufs0` (VBLK) and reads
the same8192-byte Android baseline. XNU spec_read uses buf_bread and the block
strategy with kernel cache buffers, then its own uiomove; this separately
exercises the filesystem-facing path and multi-request16KiB cache-buffer fill.
Kernel build39446089277 remains unchanged. Temporary image004 is awaiting its
screen result; do not infer a UFS filesystem mount from a block-read result.

Diagnostic005 confirms stage7 (VBLK read) fails with errno0x16/EINVAL; open and
seek succeeded and all raw checks still pass. Exact XNU specfs set_blocksize
uses d_psize as the logical sector byte size. The adapter incorrectly returned
the partition's sector count, causing spec_read's v_specsize > PAGE_SIZE check
to fail before any block-strategy I/O. d_psize now returns secsize; block count
continues to come from DKIOCGETBLOCKCOUNT. Build and real-device validation of
this change are pending.

The sector-size fix now compiles successfully: Actions39451667461 at3c98f69,
kernelSHA92111656b82eda7e1be4cd6f629c57bc7bb27f72e17ba5670bd1d4543d6e83f8.
Image006 (SHA256
0103319abed9222965605294b7022047237b0c19471b0f3f154df1a5ce0b5459) is now
hardware-verified by a photograph on2026-10-21: all four raw-path checks plus
`VFS block-device read CRC matches Android: PASS`, with continuing PID1
heartbeats. The d_psize correction fixes the observed EINVAL. Preserve006 as
the current regression image. Root remains the diagnostic HFSX RAM disk;
this does not yet prove a filesystem mount on UFS or Apple userspace.

At the user's request on2026-10-20, continue COMPUTER-ONLY preparation overnight.
The user resumed real-device testing on2026-10-21, allowing the temporary006
test above. Preserve the confirmed003/006 images, stock backups and failed-test
evidence. Destructive storage operations still need concrete per-step approval.
An official16.7.16/20H392 root candidate is prepared and checked offline; it has
not been flashed or booted. No Apple userspace/iOS GUI is verified on this K70.

Standalone probe011 now photograph-confirms32768 sparse high-RAM samples over
0x8c0000000..0x940000000 (2GiB), both pattern/restoration counters zero, with
16KiB stage1 mappings. This checks sparse addressability only. `mkvermeeradt.py
--highram` and the boot shim now support that exact profile, retaining the
256MiB default. Before any high-RAM segment copy, the shim validates the live
FDT memory ranges and fixed reservations with the bounded probe011 guard.
Unknown profiles fail. Initial high-RAM XNU tests use the proven32MiB HFS RAM
disk and source PID1, with UFS opt-in omitted: the current transport's DMA
allocation is restricted to low RAM, so high-address DMA remains untested.

The first2GiB XNU/HFS diagnostic boot is now user-reported successful after
150seconds: source PID1 starts and heartbeats continue. Kernel39451667461,
shim277d1e2, image SHA256
3c3ee5216070de48a8b46a53160e749541ee99d1885283126b8311b4ab74a7b6.
UFS opt-in was omitted; high-address storage DMA is the next separate test.

At the user's request, subsequent hardware work switches to an integrated
startup attempt rather than separate small-feature boots. The `vermeer-ios`
profile combines the K70 transport with reference software compatibility
patches through0045 (excluding the Sweet storage driver), including crypto,
entitlements/platform signatures, bindfs, key-store/watchdog stand-ins and
console crash reasons. It selects `rd=vermeerufs0` and the prepared official
16.7.16/20H392 HFSX root. No Sweet USB/touch/modem drivers are included.

The integration supports opt-in high DMA (`vermeer_ufs_highdma=1`) only inside
the tested2GiB range after UFS64AS/SMMU/page-contiguity checks. Media writes
exist only in the IOS wrapper: exact userdata geometry, journaled16GiB HFSX
header and supplied volume UUID must match before `vermeer_ufs_rw=1` enables
them. Every WRITE10 is checked again against the first16GiB of userdata;
partial4KiB writes preserve surrounding bytes. Cache synchronization is
provided. Other profiles retain write rejection. Host tests cover descriptor
upper words, capability/opt-in guards, malformed volume headers and partition/
volume bounds. The final integrated build39528953169 atbc671ba succeeded;
boot image SHA256 is
ff867146a703444e2e22f9ef8f00e019c6937ecd68c9af882fcd41245ee967bb.
Local clang14 also accepts the integrated transport. The block device presents
only the prepared16GiB volume so alternate HFS headers stay inside its bounds.
The user explicitly approved the concrete userdata overwrite and bounded root
writes. Root002 flashed successfully (99/99 chunks). A hardware photograph
then confirms the integrated kernel reaches AMFI/HFS registration and network
self-tests. UFS root mounting and Apple launchd remain unverified in that photo.
The boot host command did not return its final acknowledgment; the photograph
establishes kernel execution separately from that transport status.
