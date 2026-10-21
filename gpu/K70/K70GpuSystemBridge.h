/* SPDX-License-Identifier: MIT */
#ifndef K70_GPU_SYSTEM_BRIDGE_H
#define K70_GPU_SYSTEM_BRIDGE_H
#include <stdint.h>

/* Serialized kernel-owner contract, NOT an IOUserClient/Metal wire ABI.
 * Import IDs are owner-issued references, never CPU/GPU addresses or user
 * pointers. No service, device capability or accelerator is published here.
 * All callbacks must be real implementations; there are no success stubs.
 */
enum K70BridgeStatus {
    K70BridgeSuccess, K70BridgeBadArgument, K70BridgeNotReady,
    K70BridgeBackendFailure, K70BridgeTimeout, K70BridgeFault,
    K70BridgeQuarantined
};
enum K70BridgeState { K70BridgeUnbound, K70BridgeReady, K70BridgeActive,
                      K70BridgeRetained };
#define K70_BRIDGE_BGRA8 0x42475241u
struct K70BridgeSurface {
    uint64_t import_id, allocation_bytes, offset;
    uint32_t width, height, row_bytes, format;
};
/* Adapter reads retained K70GpuNativeResult: ME_INIT fence, then real PAS and
 * secure-exit GPU fence, then normal-mode scratch with all 1024 words checked.
 * These are observations of that run, not caller-set qualification switches.
 */
struct K70BridgeReadyEvidence {
    uint64_t lease_epoch;
    uint32_t submitted_fence, observed_fence, matched_words, secure_fence;
    uint32_t initialization_fence;
    unsigned native_complete;
};
struct K70BridgeObservation {
    uint64_t lease_epoch;
    uint32_t completed_fence, fault_flags;
};
struct K70BridgeOps {
    void *cookie;
    int (*ready_evidence)(void *, struct K70BridgeReadyEvidence *);
    int (*lease)(void *, uint64_t epoch);
    int (*now_us)(void *, uint64_t *);
    /* retain validates immutable descriptor, process entitlement/ownership at
     * the importing boundary, byte bounds, layout and nonaliasing. On error
     * it must acquire nothing. Successful imports remain pinned until release.
     */
    int (*retain)(void *, const struct K70BridgeSurface *);
    void (*release)(void *, const struct K70BridgeSurface *);
    /* Import into the OWNED native VM, validate permissions/nonaliasing,
     * publish source/cache/page tables, emit a bounded real GPU copy and a
     * CACHE_CLEAN timestamp, then ring the actual doorbell. Return its fresh
     * nonwrapping hardware fence ID. Failure can follow partial submission:
     * both imports and uncertain mappings must then remain retained forever.
     */
    int (*submit_copy)(void *, const struct K70BridgeSurface *,
                       const struct K70BridgeSurface *, uint32_t *fence);
    /* Poll real fault state and acquire GPU-written timestamp storage. Never
     * manufacture a timestamp from submission, an IRQ or CPU memcpy success.
     */
    int (*observe)(void *, uint32_t fence, struct K70BridgeObservation *);
    int (*idle)(void *);
    int (*acquire_destination)(void *, const struct K70BridgeSurface *);
    /* Qualification readback: compare every requested pixel with the pinned
     * immutable source and verify padding/guards remained intact. GPU fence
     * completion alone does not prove that the copy produced correct pixels.
     * This explicit verification may cost CPU time; it is not a performance
     * claim or an optimized UI composition implementation.
     */
    int (*verify_copy)(void *, const struct K70BridgeSurface *,
                       const struct K70BridgeSurface *);
    /* Takes its own scanout reference before returning success; must queue
     * the GPU-produced destination to the real display owner. It does not
     * turn this copy path into a CA renderer or a Metal device.
     */
    int (*present)(void *, const struct K70BridgeSurface *, uint32_t fence);
};
struct K70GpuSystemBridge {
    struct K70BridgeOps ops;
    struct K70BridgeSurface source, destination;
    uint64_t epoch, last_time_us, completed_copies, presented_copies;
    uint32_t last_fence;
    unsigned state, retained_source, retained_destination;
};
/* Zero-initialize once, keep the object/imports alive after any quarantine.
 * Owner serializes Bind/Copy and callbacks (no reentrant calls). A successful
 * fence proves one copy only; host test peers never prove phone execution.
 */
#ifdef __cplusplus
extern "C" {
#endif
enum K70BridgeStatus K70GpuSystemBridgeBind(struct K70GpuSystemBridge *,
                                          const struct K70BridgeOps *);
enum K70BridgeStatus K70GpuSystemBridgeCopy(struct K70GpuSystemBridge *,
    const struct K70BridgeSurface *, const struct K70BridgeSurface *,
    uint64_t timeout_us, unsigned poll_budget);
#ifdef __cplusplus
}
#endif
#endif
