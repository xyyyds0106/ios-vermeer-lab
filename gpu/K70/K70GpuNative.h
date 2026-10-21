/* SPDX-License-Identifier: MIT */
#ifndef K70_GPU_NATIVE_H
#define K70_GPU_NATIVE_H
#include "K70GmuNative.h"
#include "K70GpuPowerProvider.h"
#include "K70GpuClock.h"
#include "K70Zap.h"
/* Trusted kernel boot inputs, kept immutable until reboot. Firmware pointers
 * name strong local embedding definitions validated against explicit rules.
 * HFI config comes only from the acquired power provider's actual cmd-db
 * and qualified enabled K70 OPP policy; no separate startup table is accepted.
 * No struct here is an app wire API; do not pass user-controlled pointers. */
struct K70GpuNativeInput {
    K70GpuPowerRequest power;
    const uint8_t *sqe;size_t sqe_bytes;NativeAdrenoFirmwareRules sqe_rules;
    const uint8_t *gmu;size_t gmu_bytes;const K70GmuProfile *gmu_profile;
    const uint8_t *zap;size_t zap_bytes;NativeAdrenoFirmwareRules zap_rules;
    uint32_t chip_id;
    /* Owner must match the live K70 GPU-SMMU qcom,actlr SID0/mask0x3ff
     * policy for SIDs0/1/5. The saved FDT alone is not runtime qualification. */
    uint32_t smmu_actlr_qualified,smmu_actlr;
};
struct K70GpuNativeResult {
    unsigned stage,used,resources_must_retain;
    IOReturn status;
    K70ClockState clock;
    K70GmuNativeResult gmu;
    K70GpuResult gpu;
    K70ZapResult zap;
    uint32_t secure_fence;
    uint32_t smmu_id0,smmu_id1,smmu_old_actlr[2],smmu_new_actlr[2];
    unsigned gpu_bank,gmu_bank,slots[3];
};
/* Call from the owner's serialized kernel worker AFTER native C++/IOKit init.
 * This entry allocates and permanently retains the object/config/result and
 * Device MMIO mappings. It calls the real strong RPMh power interface, real
 * clock writer, allocates disabled SMMU banks/SMRs under that exclusive lease,
 * starts the actual GMU backend, initializes CP, authenticates ZAP and waits
 * for secure-mode exit before executing the native GPU scratch/fence path.
 * Nothing is registered as an accelerator. All IRQs stay controller-masked;
 * SMMU/GMU/RBBM faults are polled, HUPCF is off and failures quarantine.
 * A missing power implementation is a LINK ERROR, never a weak no-op.
 * Returns a read-only lifetime result; no automatic cleanup/retry/unmap. */
extern "C" IOReturn K70GpuNativeStart(const K70GpuNativeInput *,const K70GpuNativeResult **);
#endif
