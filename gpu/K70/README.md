# K70 native GPU execution candidate

`K70GpuNativeStart` is a kernel worker entry for the first real GPU execution
milestone. It retains the driver object, copied configuration/profile/setup,
results, backing allocations and mappings until reboot. It calls the strong
native RPMh power interface, maps exact K70 Device windows, starts clocks,
reserves disabled GPU/GMU SMMU contexts, loads GMU firmware, exchanges actual
HFI messages, obtains a fresh GPU OOB ACK, starts the SQE/CP ring and submits a
GPU scratch write followed by a CP `CACHE_CLEAN` timestamp fence.

The GPU test writes three distinct words into its own 4KB scratch mapping.
Success requires the actual submitted fence and all 1024 scratch words to
match: three GPU-written values and 1021 untouched sentinel words. The CPU
writes only the initial sentinel. No CPU framebuffer copy or synthetic fence
is used in production. ZAP PAS13 authentication and a separate secure-mode exit
packet/fence follow that kernel-only scratch stage. No accelerator, Metal
device, OpenGL ABI, display integration or application submission API is
published by this candidate.

## Native integration and remaining dependencies

The owner must link all production sources in `gpu/NativeAdreno`,
`gpu/ArmSmmu4K`, `gpu/AdrenoFirmware`, `gpu/AdrenoPM4`, and this directory, plus
the locally generated strong firmware object. Do not link the host shims.
Call the entry from a serialized kernel worker after native C++ and IOKit
initialization. Keep embedded firmware immutable until reboot. There is one
attempt; no cleanup, replay or automatic recovery path is provided.

`K70GpuPower.h` declares three **strong** native power functions. They are
dependencies on the RPMh owner's actual implementation, with no weak defaults:

- `K70GpuPowerAcquire` obtains the exclusive platform/clock/SCM/PAS/reserved
  microcode-region lease and completes qualified CX/MX/GFX and bandwidth votes.
- `K70GpuPowerValidate` checks the retained epoch, ownership and CPU idle/suspend
  inhibitor without device access.
- `K70GpuPowerQuarantine` retains any uncertain mappings, votes and ownership.

Actual ARC resources are `gfx.lvl`, `mx.lvl`, `cx.lvl`, and optional `gmxc.lvl`.
Absence of GMxC must follow a complete valid command-DB scan. GPU ARC votes use
GFX with GMxC when present, otherwise MX; GMU votes use CX/MX. BCM resources are
`SH0`, `MC0`, `ACV`, `CN0`, with original addresses and aux values. Required
startup configuration comes from the qualified K70 enabled OPP policy and the
complete immutable command DB; it must not use the sheng OPP/address tables.
`AdrenoHfiArcVotes` and `AdrenoHfiBuildBandwidth` implement the existing table
arithmetic. The native entry currently takes that already constructed config;
it does not discover or qualify these resource values itself.

The clock writer supports the reviewed boot setting GMU220MHz/HUB200MHz and
the entry restricts the first GPU test to220MHz. The K70 actual-boot FDT and
speed-bin policy must qualify the requested levels and bandwidth before power
acquisition. Cold-start preconditions are real CM3 reset asserted, GX off,
unused GPU SMMU contexts/streams, and an accessible leased domain. Wrong
existing state refuses; an unrelated live context/stream is never overwritten.
Raw native MMIO/SMC access has no external-abort containment. Power and access
preconditions must hold before each access; no probe-read helper can make an
unpowered register safe.

This source is not wired into a kernel image. The three native power functions,
complete command-DB/OPP setup builder, actual board/profile qualification,
reviewed worker/build hook and hardware validation remain integration gates.
The first image must exercise the actual execution path; host results below
do not establish that GPU execution has occurred on a phone.

## K70 adaptations

- GMU core window is `0x03d68000+0x37000`; GPU-relative register indices
  subtract `0x68000`. The old sheng origin `0x6a000` is not reused. The GMU
  counter low offset is `0x16220` within the named K70 GMU window.
- GPU SMMU is `0x03da0000+0x40000`, independent of the application SMMU.
  Under the exclusive whole-device lease, the allocator verifies disabled
  banks and unused SMRs before assigning GPU bank0 and a distinct GMU bank1.
  SIDs0/1/5 are routed through allocated slots, not assumed SID==slot.
- CPU ownership remains native16KB while GPU lengths/tables remain4KB.
  GMU payloads become non-cacheable through exclusively owned native pages
  before being mapped NC; no second cacheable/NC descriptor alias is created.
  GPU buffers use matching WB attributes with explicit publication/acquisition;
  no unproved cache-coherent skip is enabled. K70's GPU SMMU FDT declares
  `dma-coherent`, supporting the table-walk policy.
- Fault continuation is disabled. The generic SMMU codec retains CFIE/GFIE;
  this entry verifies all K70 GPU/GMU/SMMU IRQs already disabled/inactive at
  the GIC and never enables or clears them. The platform lease must prevent
  another consumer from unmasking those IRQs. SMMU FSR, GMU health and RBBM/CP
  faults are polled. No fault IRQ handler is claimed.
- Ring fence waits recheck time after cache callbacks, reject backwards time
  and future fence values, and have an iteration bound for a frozen clock.
  Cold attach refuses a retained nonzero RPTR before SQE can consume unwritten
  ring words. Fence IDs cannot wrap to zero. Failed/expired work does not retire resources.
- Official SoC ZAP is authenticated through real SMCCC/PAS13 return values.
  Its reserved K70 range is `0x9b09a000+0x2000`; the temporary unmanaged IO
  mapping does not change neighbouring native16KB-page cache attributes.
  A failed PAS, interrupted/busy/deadline failure or secure fence stops entry.

## Firmware identity

The fixed public candidate is Qualcomm's linux-firmware commit
[`30979b116b5c5857b72c4332db8db0ff1ca2dc08`](https://kernel.googlesource.com/pub/scm/linux/kernel/git/firmware/linux-firmware/+/30979b116b5c5857b72c4332db8db0ff1ca2dc08).
`prepare_firmware.py` verifies all complete hashes and parses the GMU block
count/MDT extent before emitting local Mach-O `.incbin` definitions outside the
checkout. It performs no download or device operation. Missing objects fail
linking; `K70GpuOfficialFirmware` additionally hashes all three native views.

| Candidate | Bytes | SHA256 |
| --- | ---: | --- |
| `gmu_gen70200.bin` |66844|`1a2a419c39046d3141fc5fed5aa7f971de2db40cc7a1d89693c3e26fad64dd98`|
| `a740_sqe.fw` v1.81 |76724|`96fee336424b139100fc60b5b45a907360e4b3936d7e1d00406b9bd80ca48473`|
| `sm8550/a740_zap.mbn` v0.11 |12088|`386bbdc25ae94a9398e33e7580eecfdcef6c472682a1e3123e1374bcffe7cde3`|

No firmware bytes, generated firmware object, private dump or log belongs in
Git. Fixed byte identity is distinct from real GMU loading/protocol and PAS
authentication on K70. The complete qualification profile and RSC/PDC setup
must be constructed from reviewed K70/catalog source and retained privately.
They cannot be marked hardware-qualified solely because a hash matches.

## Verification

Run `CC=clang CXX=clang++ sh tests/k70gpu/run-core-tests.sh`. ASan/UBSan executes
production memory/SMMU/VM/CP/ring code over an explicitly separate software CP
peer, including real emitted packets,16KB backing with4KB GPU exposure, no
execution, missing write/fence, future fence, corruption and revoked leases.
The clock writer covers133 access failure cutoffs plus timeouts/replay/frozen
time. Additional tests exercise production GMU firmware/profile/preflight
faults, refused native reset and SCM, bounded fence timing, and the actual
retained native entry refusing its power boundary before mapping anything.
The host never authenticates firmware or synthesizes a GMU ACK. A software CP
peer success is not hardware execution evidence.

All native components are also checked with retained K70 ARM64/16KB XNU
compiler recipes. That is a syntax check, not a complete kernel link or boot.
`UPSTREAM.json` records original source hashes and local changes; component
licenses are retained beside the sources.
