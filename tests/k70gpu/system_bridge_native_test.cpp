/* SPDX-License-Identifier: MIT
 * Structural result-adapter test only, not GPU evidence. */
#include "../../gpu/K70/K70GpuSystemBridgeNative.h"
#include <cassert>
#include <cstdio>
int main()
{
    K70GpuNativeResult valid{};
    valid.used = valid.resources_must_retain = 1;
    valid.status = valid.gmu.status = valid.gpu.status = kIOReturnSuccess;
    valid.gmu.ready = valid.zap.authenticated = 1;
    valid.gpu.stage = K70GpuComplete;
    valid.gpu.hardware_complete = valid.gpu.cp_ready = valid.gpu.normal_mode_ready = 1;
    valid.gpu.cp_fence = 1;
    valid.secure_fence = 2;
    valid.gpu.write_fence = valid.gpu.observed_fence = 3;
    valid.gpu.matched_words = 1024;
    K70BridgeReadyEvidence out{};
    assert(!K70GpuSystemBridgeNativeEvidence(&valid, 9, &out));
    assert(out.initialization_fence == 1 && out.secure_fence == 2 &&
           out.submitted_fence == 3 && out.observed_fence == 3 && out.lease_epoch == 9);
    for (unsigned field = 0; field < 12; ++field) {
        auto bad = valid;
        if (field == 0) bad.status = kIOReturnNotReady;
        if (field == 1) bad.gmu.ready = 0;
        if (field == 2) bad.zap.authenticated = 0;
        if (field == 3) bad.gpu.cp_ready = 0;
        if (field == 4) bad.gpu.normal_mode_ready = 0;
        if (field == 5) bad.gpu.hardware_complete = 0;
        if (field == 6) bad.gpu.fault_flags = 1;
        if (field == 7) bad.gpu.observed_fence = 2;
        if (field == 8) bad.gpu.matched_words = 1023;
        if (field == 9) bad.secure_fence = 4;
        if (field == 10) bad.gpu.cp_fence = 0;
        if (field == 11) bad.gpu.stage = K70GpuCp;
        assert(K70GpuSystemBridgeNativeEvidence(&bad, 9, &out));
        assert(!out.native_complete && !out.lease_epoch);
    }
    assert(K70GpuSystemBridgeNativeEvidence(&valid, 0, &out));
    assert(K70GpuSystemBridgeNativeEvidence(nullptr, 9, &out));
    std::puts("K70 native bridge evidence adapter tests passed (structure only)");
}
