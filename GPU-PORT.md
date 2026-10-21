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

Upstream code is retained privately for review. Any vendored source will retain
its component license; NativeAdreno/NativeMetal/ArmSmmu4K use MIT and several
platform kits use BSD-2-Clause-Patent. Apple images, device backups, private
captures and extracted firmware are not included in this repository.
