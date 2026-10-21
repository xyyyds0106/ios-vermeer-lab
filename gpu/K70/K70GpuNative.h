/* SPDX-License-Identifier: MIT */
#ifndef K70_GPU_NATIVE_H
#define K70_GPU_NATIVE_H
#include "K70GmuNative.h"
#include "K70GpuPower.h"
#include "K70GpuClock.h"
#include "K70Zap.h"
/* Trusted kernel boot inputs, kept immutable until reboot. Firmware pointers
 * name strong local embedding definitions validated against explicit rules.
 * HFI config is built from actual cmd-db/qualified enabled K70 OPP policy.
 * No struct here is an app wire API; do not pass user-controlled pointers. */
struct K70GpuNativeInput {
    K70GpuPowerRequest power;
    const uint8_t *sqe;size_t sqe_bytes;NativeAdrenoFirmwareRules sqe_rules;
    const uint8_t *gmu;size_t gmu_bytes;const K70GmuProfile *gmu_profile;
    const uint8_t *zap;size_t zap_bytes;NativeAdrenoFirmwareRules zap_rules;
    const AdrenoHfiStartupConfig *startup;
    uint32_t chip_id,perf_index,bandwidth_index;
};
struct K70GpuNativeResult {
    unsigned stage,used,resources_must_retain;
    IOReturn status;
    K70ClockState clock;
    K70GmuNativeResult gmu;
    K70GpuResult gpu;
    K70ZapResult zap;
    uint32_t secure_fence;
    unsigned gpu_bank,gmu_bank,slots[3];
};
/* Call from the owner's serialized kernel worker AFTER native C++/IOKit init.
 * This entry allocates and permanently retains the object/config/result and
 * Device MMIO mappings. It calls the real strong RPMh power interface, real
 * clock writer, allocates disabled SMMU banks/SMRs under that exclusive lease,
 * starts the actual GMU backend and executes the native GPU scratch/fence path.
 * Nothing is registered as an accelerator. All IRQs stay controller-masked;
 * SMMU/GMU/RBBM faults are polled, HUPCF is off and failures quarantine.
 * A missing power implementation is a LINK ERROR, never a weak no-op.
 * Returns a read-only lifetime result; no automatic cleanup/retry/unmap. */
extern "C" IOReturn K70GpuNativeStart(const K70GpuNativeInput *,const K70GpuNativeResult **);
#endif
