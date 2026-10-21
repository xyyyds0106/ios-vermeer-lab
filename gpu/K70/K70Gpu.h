/* SPDX-License-Identifier: MIT */
#ifndef K70_GPU_H
#define K70_GPU_H
#include "../NativeAdreno/NativeAdrenoSmmuContext.h"
#include "../NativeAdreno/NativeAdrenoVm.h"
#include "../NativeAdreno/NativeAdrenoCpInit.h"
#include "../NativeAdreno/NativeAdrenoIrq.h"
#include "../NativeAdreno/NativeAdrenoFirmware.h"

enum K70GpuStage {K70GpuUnused,K70GpuPlatform,K70GpuFirmware,K70GpuGmu,
    K70GpuAllocation,K70GpuContext,K70GpuHardware,K70GpuCp,K70GpuScratch,K70GpuComplete};
struct K70GpuWindow {volatile uint8_t *cpu;uint64_t physical;size_t bytes;};
/* Pinned, kernel-only platform contract. Nothing here is a user ABI. All
 * storage and the object itself remain alive forever after an attempt.
 * The owner serializes run() and never releases power/mappings on failure.
 * reserve must obtain EXCLUSIVE GPU and GMU stream/bank ownership, real
 * CX/GX/clock/RPMh leases, mask/handle faults and verify the board/silicon.
 * lease is a bounded in-memory check of that retained ownership, no discovery.
 * boot_gmu must execute real GMU firmware/HFI and GPU OOB/GX readiness, with
 * ACTLR/platform/security prerequisites for these exact owned streams. It
 * must not return success from a flag or a CPU-written ACK. Missing hooks
 * refuse before any allocation or MMIO; there are no weak empty defaults. */
struct K70GpuPlatformOps {
    void *cookie;
    IOReturn (*reserve)(void *);
    int (*lease)(void *);
    int (*now_us)(void *,uint64_t *);
    int (*idle)(void *);
    IOReturn (*boot_gmu)(void *);
};
struct K70GpuConfig {
    K70GpuPlatformOps platform;
    K70GpuWindow gpu,gmu,smmu;
    /* Allocator-issued bank/ASID/SMR slots, never inferred from DT SID. */
    unsigned bank,asid,slots[2];
    uint16_t sids[2];
    /* Board catalog attestation: NOT the DT compatible alone. The imported
     * A740 programming profile must be verified for the actual chip revision. */
    unsigned a740_profile_verified;
    const uint8_t *sqe;size_t sqe_bytes;
    NativeAdrenoFirmwareRules sqe_rules;
    uint64_t total_timeout_us;
};
struct K70GpuResult {
    K70GpuStage stage;IOReturn status;
    uint32_t cp_fence,write_fence,observed_fence,matched_words;
    uint32_t scratch[4],fault_flags;
    uint64_t elapsed_us;
    size_t logical_bytes[5],cpu_owned_bytes[5],gpu_spans[5];
    unsigned resources_must_retain,hardware_complete,cp_ready,normal_mode_ready;
};
/* Real native allocation -> native4K SMMU context -> CP/ring -> GPU write ->
 * CP CACHE_CLEAN timestamp -> CPU acquire+verify. run() stops after CP init;
 * the owner authenticates ZAP, calls leaveSecureAfterPas(), then verifies the
 * scratch submission with verifyScratch(). No CPU stores write a success
 * pattern or a completed fence. One attempt, no teardown/reset guess. This
 * object has no user submission entry and never publishes an accelerator. */
class K70Gpu final {
    K70GpuConfig c_{};K70GpuResult *r_=nullptr;
    NativeAdrenoMemory tables_,bo_[5];NativeAdrenoSmmuContext context_;
    NativeAdrenoVm vm_;NativeAdrenoRing ring_;NativeAdrenoCpInit cp_;NativeAdrenoIrq irq_;
    uint64_t handles_[5]{},begin_=0,last_=0;
    bool used_=false,held_=false,failed_=false,scratch_attempted_=false;
    ArmSmmu4KIo smmu_io_{};NativeAdrenoRingIo ring_io_{};
    int guard();IOReturn fail(IOReturn);
    static int gpuRead(void *,uint32_t,uint32_t *);
    static int gpuWrite(void *,uint32_t,uint32_t);
    static int barrier(void *);
    static int smmuRead32(void *,size_t,uint32_t *);
    static int smmuRead64(void *,size_t,uint64_t *);
    static int smmuWrite32(void *,size_t,uint32_t);
    static int smmuWrite64(void *,size_t,uint64_t);
    static int publish(void *,const volatile uint32_t *,size_t);
    static int acquire(void *,volatile uint32_t *,size_t);
    static uint64_t now(void *);
    static int idle(void *);
    int hwInit();
public:
    K70Gpu()=default;K70Gpu(const K70Gpu &)=delete;K70Gpu &operator=(const K70Gpu &)=delete;
    IOReturn run(const K70GpuConfig *,K70GpuResult *);
    /* Kernel lifecycle owner calls ONLY after real PAS13 authentication.
     * Emits CP_SET_SECURE_MODE0 and waits on another actual CP fence. */
    IOReturn leaveSecureAfterPas(uint32_t *fence);
    /* Kernel-only one-shot submission proof; refuses before the completed
     * secure-mode exit fence. No application submission ABI is exposed. */
    IOReturn verifyScratch();
};
#endif
