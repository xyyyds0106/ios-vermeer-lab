# UniXNU Adreno 740 integration

User-provided reference: [UniXNU](https://github.com/UniXNU/UniXNU).
Its [native Adreno documentation](https://unixnu.com/drivers/native-adreno)
reports working Metal devices and GPU UI compositing on the SM8550 Xiaomi Pad
6S Pro. That is upstream evidence; this K70 build still uses CPU framebuffer
copies and has no operational native Adreno/Metal backend.

The reviewed snapshot is UniXNU `fb2bb310564ce66779e472a021881cf7c5764919`,
with these pinned submodules:

| Component | Commit | Reuse boundary |
| --- | --- | --- |
| NativeAdreno | `805c7e4bfb7451eb6923f4a14dea8c657b9be7d2` | GMU, GPU contexts, buffers, submissions and fences |
| NativeMetal | `6dcd4cbb69eb5625988706de42aa8ad3009b8ee7` | Metal frontend, compiler bridge and presentation client |
| NativeMobileDisplay | `eb9c949164826c2e52f2771eac19e4b8bd8a3dae` | GPU scanout offers and retirement |
| ArmSmmu4K | `4d0d6598b1a36aeec3de01091893fa6660e49388` | GPU address translation and owned stream/context lifecycle |
| AdrenoFirmware | `40fdb8ea87606612ac6f1e80bbb49955d0ce4051` | GMU firmware format, HFI startup and power-vote protocols |
| AdrenoPM4 | `445f8fe6d115738352b9d78dc70af6d61aa7f62f` | A740 command and shader-state construction |

Important differences established by reading actual source:

- Upstream sheng uses iPadOS17.7.11 and a source XNU with4KB CPU pages. The
  current K70 root is iOS16.7.16 and its CPU kernel has16KB pages.
- `NativeAdrenoQueryCpu4K` explicitly checks both CPU TCR granules and kernel/map
  page sizes. Cache-attribute operations use physical-page numbers shifted by12.
  Removing that guard alone would corrupt the ownership/cache model on16KB XNU.
  CPU allocation/cache granules must be kept separate from GPU4KB SMMU granules;
  imported buffers and pooled suballocations need whole CPU-page ownership.
- The accelerator builder binds audited original IOAcceleratorES constructor,
  destructor and metaclass addresses. Those addresses belong to the upstream
  target and cannot be reused for this source kernel.
- The Metal plugin contains target-specific QuartzCore display-state reads and
  writes. The iOS16 ABI/layout must be established before enabling equivalent
  hooks. Upstream's published-device marker is not a K70 GPU enable switch.
- Board power, GMU command database/votes, firmware, stream IDs and scanout
  ownership must come from the K70 hardware contract. Matching the GPU model
  does not establish context-bank ownership or electrical readiness.

## Compile compatibility checked

Eighteen selected, unmodified upstream translation units passed syntax checking
with the retained K70 XNU kernel's actual ARM64/16KB build configuration:

- NativeAdreno board parsing, backing memory, CPU/cache operations, SMMU memory
  visibility and GPU virtual-memory management (five C++ files).
- ArmSmmu4K tables, audit, context, live mappings, MMIO binding, programming,
  streams and table memory (eight C files).
- AdrenoFirmware planning plus HFI, HFI startup and votes (four C files).
- AdrenoPM4 packet encoding (one C file).

This establishes a useful source/API reuse baseline. It is not a linked driver,
an exercised 16KB allocation/cache path, a firmware upload or a GPU test.
In particular, the unmodified CPU guard still refuses this kernel's page size.
Host test programs are separate from these kernel translation units; attempting
to compile a host test against kernel headers is not an ABI compatibility test.

The page-size port must retain requested GPU buffer lengths separately from
CPU-owned allocation extents. The latter require whole 16KB ownership before
changing cache attributes; GPU mappings and table pages remain 4KB. Pool splits,
imported user ranges, cache-attribute restoration and user mapping teardown all
need the same ownership rule. Padding cannot silently become a GPU-accessible
part of a neighbouring allocation.

The sheng board code also uses a different GMU register-window origin from the
saved K70 tree. Resolve named register ranges and register-offset bases before
reusing its generated GMU setup; identical silicon does not make those two
window-relative address calculations interchangeable.

Read-only parsing of the previously saved K70 DT confirms GPU MMIO at0x3d00000,
the GMU window at0x3d68000, and a separate GPU SMMU at0x3da0000. These are not
the0x15000000 application SMMU used by the current UFS/USB path. No GPU MMIO,
power votes or context changes have been performed by this integration work.

The existing media-service failure is separately localized: CMCapture portrait
prewarming creates a CoreImage GL context, which calls a null GL function-table
entry. This is consistent with the missing graphics backend; a causal link to
the reported Hello still picture has not been proved. No unverified environment
override or fake accelerator service is used to hide this failure.

The immediate independent integration benefit is the same SM8550 GENI SPI
contract: wrapper1 core2x/core clock votes were missing from the initial K70
physical-touch candidate. Their offsets/bits were cross-checked against pinned
MiCode Kalama clock source. The touch repair also follows its HWCG readiness
rules. These changes do not claim GPU hardware initialization.

The subsequent K70 temporary boot passed that clock preflight and identified SPI
firmware with the FIFO interface disabled. Physical input therefore still needs
the GENI internal-DMA transport and the verified K70 power/reset sequence. The
driver stopped before a touch transaction; it did not turn that into an input
success. The upstream sheng probe's SMMU-bypass operation is also not a substitute
for establishing ownership of this board's DMA stream/context.

## Local 16KB memory port

`gpu/` now contains pinned open-source memory, SMMU, PM4 and HFI foundations,
with component licenses and upstream hashes in `gpu/UPSTREAM.json`. The memory
port separates requested GPU bytes from exclusively owned CPU-page extents.
Allocations and pool splits own whole native 4KB or16KB pages, while exposed
GPU spans retain4KB granularity and do not expose allocation padding. Cache
attribute operations use the kernel's native PAGE_SHIFT. Imported user memory
keeps its cache attributes and is maintained through physical spans.

Read-only imports refuse GPU-write publication and write acquisition. A pool
cannot become write-combined while it has live allocations. Outstanding device
references refuse release; failed completion/cache restoration quarantines the
allocation. Device references still require real completion/unmapping by the
future submission driver.

Run `CC=clang CXX=clang++ sh tests/run-gpu-touch-tests.sh`. ASan/UBSan tests
execute the production memory implementation with injected descriptor, cache
and allocation failures, native4KB/16KB layouts, hidden padding, imports and pool
lifetime checks. All15 local GPU translation units also pass syntax compilation
with the retained K70 ARM64/16KB kernel recipe. ARM cache instructions and real
device memory visibility remain hardware tests.

These sources are not included in the kernel profile or registered as an
accelerator. GMU power/firmware, owned GPU SMMU contexts, submission/fences,
IOAccelerator/Metal ABI and scanout integration remain unfinished. Current
images still render through CPU copies. No new GPU-enabled boot image is
produced by these tests.

NativeAdreno/NativeMetal/ArmSmmu4K use MIT and several platform kits use
BSD-2-Clause-Patent. Apple images, device backups, private captures and extracted
firmware are not included in this repository.
