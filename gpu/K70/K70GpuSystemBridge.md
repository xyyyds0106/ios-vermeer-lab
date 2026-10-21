# Kernel copy qualification bridge

`K70GpuSystemBridge.c` implements a serialized, bounded lifecycle for a future
native GPU copy provider. It is not currently linked into the phone image and
has no real copy provider. It does not register an accelerator, implement a
Metal device, translate shaders, or change the CPU framebuffer backend.

The existing `K70GpuNativeStart` execution milestone and the system graphics
path are separate. Its owner retains the GMU/CP/VM objects, but `K70Gpu` currently
has no public arbitrary copy submission method. A passed scratch test cannot
serve as that missing method. Connecting this bridge therefore still requires
an implementation that imports/pins both surfaces, emits actual GPU work and
observes its GPU-written fence. Missing callbacks are errors.

## Contract and qualification

The owner zero-initializes the bridge and serializes every call. This API is
kernel-only: import IDs are opaque owner references, never user pointers,
physical addresses, or IOVAs. A future user client must validate task ownership,
entitlements, sizes and fixed wire records before invoking it.

Binding reads retained native evidence in actual execution order: ME_INIT
fence, PAS authentication and secure-exit fence, then normal-mode scratch with
all 1024 words verified. This is a read of actual results, not permission to
set qualification fields. Bind checks their nonzero, increasing fence IDs,
matching observed scratch fence and retained lease epoch.
`K70GpuSystemBridgeNative.h` supplies the read-only adapter from the actual
native result fields, including GMU readiness, authenticated ZAP, CP/normal-mode
readiness, final GPU stage and absence of faults. It cannot replace the owner's
live lease validation or create a real copy submitter.

Each copy accepts equal-size linear BGRA8 surfaces with independent row strides
and bounded byte ranges. It retains both imports, submits through the real
backend, polls the actual completion/fault state, acquires destination caches,
verifies all copied pixels and untouched padding/guards, and only then calls
the display owner. The display callback must take its own scanout reference
before returning. Copy, verify and present callbacks must be bounded and never
call back into the bridge. Verification is an explicit qualification cost;
this code makes no performance claim.

Lease revocation, backwards time, deadline/poll exhaustion, a future fence,
fault or data mismatch quarantine the bridge. After submission becomes possible,
uncertain imports and mappings stay retained until reboot. There is no retry,
reset, guessed cleanup, CPU success-copy fallback, or synthesized completion.
Epoch/time are checked again after presentation before releasing imports.

Run `CC=clang-14 sh tests/k70gpu/run-system-bridge-tests.sh`. ASan/UBSan tests
exercise an explicitly separate software callback peer: normal/repeated work,
missing/old/future fences, bad boot evidence, invalid/overflowed surface ranges,
cache/readback/display failures, lease loss, clocks, poll exhaustion and retained
resources. These tests establish lifecycle policy only, never phone execution.

## What the original system still requires

The following local evidence is read-only reference material. It is not copied
into this bridge and does not qualify an ABI for the current image.

* The original 20H392 ARM64 cache catalogue includes `Metal.framework/Metal`,
  `QuartzCore.framework/QuartzCore`, `IOAccelerator.framework/IOAccelerator`,
  `IOGPU.framework/IOGPU`, and `AGXMetalA11.bundle/AGXMetalA11`. Existence proves
  which consumers are present, not their method layouts or compatibility with
  Adreno. Catalogue: `../device-audit/d22ap-cache-info.json` outside the repo.
* The retained upstream `NativeAdreno/NativeAdrenoABI.h` defines its own
  versioned BO/import/map/queue/submit/wait-fence records. Its header explicitly
  distinguishes that ABI from Metal and IOKit. A producer using those records
  needs the corresponding kernel dispatcher and scheduling/VM implementation;
  original Apple clients do not automatically speak them.
* Upstream `NativeAdreno/NativeAdrenoService.cpp` supplies a real `IOService` /
  `IOUserClient` surface, external method dispatch and client memory mapping,
  gated on a started CP. Its board code contains sheng-specific setup and is
  not a qualified K70 port.
* Upstream `NativeMetal/NativeAdrenoMetalPlugin.mm` describes discovery through
  an accelerator service, `MetalPluginName`, bundle principal class and
  `initWithAcceleratorPort:`. Its default probe returns nil; actual rendering
  requires the NativeMetal frontend, AIR translation and Turnip. The current
  system's exact discovery/selector behavior must be independently confirmed
  against the original cache. A plugin probe is not a Metal implementation.
* Upstream `NativeMetal/OriginalAcceleratorABI.h` explicitly qualifies only a
  supplied iOS17 base and a 136-byte class extent. Its private constructor,
  vtable/metaclass imports and size cannot be reused for 20H392 on that claim.
* Upstream `NativeAdreno/NativeAdrenoAccelerator.cpp` deliberately names its
  class `AGXAccelerator` to match an Apple sandbox class. That strategy is
  excluded here. A K70 service must use its own identity and explicitly
  authorized user-client access; no Apple GPU impersonation or fake caps.

The required native build headers include `IOKit/IOService.h`,
`IOKit/IOUserClient.h`, `IOKit/IOMemoryDescriptor.h`, `IOKit/IODeviceMemory.h`,
`IOKit/IOLib.h`, native task/memory APIs and the actual surface owner's interface.
This project currently owns a `SweetSurface`/`AppleCLCD` implementation, rather
than a proven original IOSurface/IOGPU userspace contract. The actual
`SweetFramebuffer.cpp` swap retains surfaces; present uses `memcpy` per row.
Replacing that copy is a distinct display step and still does not implement
CoreAnimation rendering or Metal command submission.

An executable system path needs all of these gates: real K70 power/GMU/normal
mode and scratch completion; persistent owned contexts and a GPU copy/render
scheduler; IOSurface import and cache/lease/fence ownership; a real display
handoff; a K70-named IOKit service and validated wire ABI; and a userspace Metal
frontend plus actual shader translation/graphics commands that CoreAnimation
can use on this OS build. Publish graphics capabilities only after those
operations have been verified on hardware. This bridge isolates one lifecycle
component; it does not claim that the system graphics stack is connected.
