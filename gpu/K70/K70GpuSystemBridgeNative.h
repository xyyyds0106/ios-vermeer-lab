/* SPDX-License-Identifier: MIT */
#ifndef K70_GPU_SYSTEM_BRIDGE_NATIVE_H
#define K70_GPU_SYSTEM_BRIDGE_NATIVE_H
#include "K70GpuNative.h"
#include "K70GpuSystemBridge.h"
/* Read-only adapter for the retained actual native result. The owner supplies
 * its retained platform epoch; Bind separately checks that live epoch lease.
 * Never accept these structures from an external method or construct native
 * results from requested capabilities. This checks evidence, not hardware.
 */
static inline int K70GpuSystemBridgeNativeEvidence(
    const K70GpuNativeResult *native, uint64_t epoch,
    K70BridgeReadyEvidence *out)
{
    if (!out) return 1;
    *out = {};
    if (!native || !epoch || !native->used || !native->resources_must_retain ||
        native->status != kIOReturnSuccess || !native->gmu.ready ||
        native->gmu.status != kIOReturnSuccess || !native->zap.authenticated ||
        native->gpu.status != kIOReturnSuccess ||
        native->gpu.stage != K70GpuComplete || !native->gpu.hardware_complete ||
        !native->gpu.cp_ready || !native->gpu.normal_mode_ready ||
        native->gpu.fault_flags || !native->gpu.cp_fence ||
        native->secure_fence <= native->gpu.cp_fence ||
        native->gpu.write_fence <= native->secure_fence ||
        native->gpu.observed_fence != native->gpu.write_fence ||
        native->gpu.matched_words != 1024)
        return 1;
    out->lease_epoch = epoch;
    out->initialization_fence = native->gpu.cp_fence;
    out->secure_fence = native->secure_fence;
    out->submitted_fence = native->gpu.write_fence;
    out->observed_fence = native->gpu.observed_fence;
    out->matched_words = native->gpu.matched_words;
    out->native_complete = 1;
    return 0;
}
#endif
