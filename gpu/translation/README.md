# Bounded buffer-fill lowering candidate

`AdrenoFillTranslate` lowers a typed internal buffer-fill request into one
Adreno type-7 `CP_MEM_WRITE` packet. It is an isolated host-tested building block:
it is not linked into the kernel or GUI013 and does not register a Metal device.

The supported operation fills 4–4096 bytes with one byte value. Both offset and
length must be multiples of four. Version 1 accepts only buffer fill; shaders,
textures, copy, draw, zero-length and other alignments return an explicit error.
The request is internal IR after an eventual real API decoder, not an iOS16
Metal, Objective-C, OpenGL or client wire ABI implementation.

The resource owner must supply a trusted resolved GPU view and keep its real
mapping and hardware lease alive through submission and observed completion.
Buffer identity, generation and context must match. Logical buffer length bounds
access independently of native 16KB CPU allocation padding. Accepted IOVA widths
are 32, 36, 40, 42, 44 and 48 bits. The view flags assert caller preconditions;
they cannot obtain or independently prove ownership. Never accept a client GPU
address as this trusted view.

Successful return means packet encoding only. `requires_completion=1` explicitly
requires the eventual backend to perform cache/visibility ordering, submit using
an owned GPU context, observe a real fence and retain/quarantine resources until
completion or proven retirement. This function allocates nothing and touches no
hardware. The output contains a write command, so it must not be accepted as
arbitrary client command submission. On failure, output words and plan remain
unchanged and the word count is zero (when its pointer is supplied). All input
and output storage must be disjoint, accessible and stable during the call.

## Packet basis and verification

The existing local `AdrenoPM4Type7` encoder is used without modification. Its
upstream pin is UniXNU/AdrenoPM4 commit
`445f8fe6d115738352b9d78dc70af6d61aa7f62f` in `gpu/UPSTREAM.json`.
The opcode and low/high address followed by data words are supported by the
public Qualcomm implementation at LineageOS/android_kernel_xiaomi_sm8550-modules
commit `92a9fde479d3262da080cfd28bab0775b0da500a`:

- [CP_MEM_WRITE definition](https://github.com/LineageOS/android_kernel_xiaomi_sm8550-modules/blob/92a9fde479d3262da080cfd28bab0775b0da500a/qcom/opensource/graphics-kernel/adreno_pm4types.h#L69)
- [Gen7 ringbuffer packet construction](https://github.com/LineageOS/android_kernel_xiaomi_sm8550-modules/blob/92a9fde479d3262da080cfd28bab0775b0da500a/qcom/opensource/graphics-kernel/adreno_gen7_ringbuffer.c#L40)

Run `sh tests/translation/run-fill-tests.sh`. The tests compile the actual
translator, PM4 encoder and PM4 decoder with ASan/UBSan. All 6144 combinations
of 256 fill values, six lengths and four offsets pass the real PM4 decoder and a
separately implemented software memory-effect oracle. The oracle checks packet
parity, opcode, length, address and every byte including the untouched region.
Additional tests cover all insufficient capacities, stale identity/generation/
context, missing lease/write flags, unsupported requests, CPU padding isolation,
IOVA endpoints for all six widths, invalid inputs and a fixed packet vector.
These are host checks; no GPU executes these packets.

Integration still needs the board power/clock provider, GMU firmware and HFI,
GPU SMMU ownership, CP/ring initialization, hardware completion and failure
retirement, followed by a verified iOS16 API/ABI bridge. This candidate proves
none of those prerequisites and is deliberately outside that integration path.
