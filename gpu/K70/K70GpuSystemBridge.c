/* SPDX-License-Identifier: MIT */
#include "K70GpuSystemBridge.h"

static int surface_valid(const struct K70BridgeSurface *s)
{
    if (!s || !s->import_id || s->format != K70_BRIDGE_BGRA8 ||
        !s->width || !s->height || s->width > 8192 || s->height > 8192 ||
        (s->row_bytes & 3) || (uint64_t)s->width * 4 > s->row_bytes ||
        s->allocation_bytes > 268435456 || (s->offset & 3) ||
        s->offset > s->allocation_bytes)
        return 0;
    uint64_t extent = (uint64_t)(s->height - 1) * s->row_bytes +
                      (uint64_t)s->width * 4;
    return extent <= s->allocation_bytes - s->offset;
}
static enum K70BridgeStatus retain_failure(struct K70GpuSystemBridge *b,
                                          enum K70BridgeStatus status)
{
    b->state = K70BridgeRetained;
    return status;
}
static int guard(struct K70GpuSystemBridge *b, uint64_t begin,
                 uint64_t timeout, int timed)
{
    uint64_t now;
    if (b->ops.now_us(b->ops.cookie, &now) || now < b->last_time_us ||
        b->ops.lease(b->ops.cookie, b->epoch))
        return 1;
    b->last_time_us = now;
    return timed && now - begin >= timeout ? 2 : 0;
}
enum K70BridgeStatus K70GpuSystemBridgeBind(struct K70GpuSystemBridge *b,
                                          const struct K70BridgeOps *ops)
{
    struct K70BridgeReadyEvidence e = {0};
    uint64_t now;
    if (!b || !ops || !ops->ready_evidence || !ops->lease || !ops->now_us ||
        !ops->retain || !ops->release || !ops->submit_copy || !ops->observe ||
        !ops->idle || !ops->acquire_destination || !ops->verify_copy || !ops->present)
        return K70BridgeBadArgument;
    if (b->state != K70BridgeUnbound) return K70BridgeNotReady;
    if (ops->ready_evidence(ops->cookie, &e) || !e.native_complete ||
        !e.lease_epoch || !e.submitted_fence ||
        e.submitted_fence != e.observed_fence || e.matched_words != 1024 ||
        !e.initialization_fence || e.secure_fence <= e.initialization_fence ||
        e.submitted_fence <= e.secure_fence ||
        ops->lease(ops->cookie, e.lease_epoch) ||
        ops->now_us(ops->cookie, &now))
        return K70BridgeNotReady;
    b->ops = *ops;
    b->epoch = e.lease_epoch;
    b->last_fence = e.observed_fence;
    b->last_time_us = now;
    b->state = K70BridgeReady;
    return K70BridgeSuccess;
}
enum K70BridgeStatus K70GpuSystemBridgeCopy(struct K70GpuSystemBridge *b,
    const struct K70BridgeSurface *src, const struct K70BridgeSurface *dst,
    uint64_t timeout, unsigned budget)
{
    if (!b) return K70BridgeBadArgument;
    if (b->state == K70BridgeRetained) return K70BridgeQuarantined;
    if (b->state != K70BridgeReady) return K70BridgeNotReady;
    if (!src || !dst) return K70BridgeBadArgument;
    const struct K70BridgeSurface source = *src, destination = *dst;
    src = &source;
    dst = &destination;
    if (!surface_valid(src) || !surface_valid(dst) ||
        src->width != dst->width || src->height != dst->height ||
        src->import_id == dst->import_id || !timeout || timeout > 5000000 ||
        !budget || budget > 100000 || b->completed_copies == UINT64_MAX ||
        b->presented_copies == UINT64_MAX)
        return K70BridgeBadArgument;
    b->state = K70BridgeActive;
    if (guard(b, 0, 0, 0))
        return retain_failure(b, K70BridgeBackendFailure);
    uint64_t begin = b->last_time_us;
    b->source = *src;
    b->destination = *dst;
    if (b->ops.retain(b->ops.cookie, &b->source)) {
        b->state = K70BridgeReady;
        return K70BridgeBackendFailure;
    }
    b->retained_source = 1;
    int checked = guard(b, begin, timeout, 1);
    if (checked) return retain_failure(b, checked == 2 ? K70BridgeTimeout :
                                       K70BridgeBackendFailure);
    if (b->ops.retain(b->ops.cookie, &b->destination)) {
        /* Nothing was submitted: this successful source retain is releasable. */
        checked = guard(b, begin, timeout, 1);
        if (checked) return retain_failure(b, checked == 2 ? K70BridgeTimeout :
                                           K70BridgeBackendFailure);
        b->ops.release(b->ops.cookie, &b->source);
        b->retained_source = 0;
        checked = guard(b, begin, timeout, 1);
        if (checked) return retain_failure(b, checked == 2 ? K70BridgeTimeout :
                                           K70BridgeBackendFailure);
        b->state = K70BridgeReady;
        return K70BridgeBackendFailure;
    }
    b->retained_destination = 1;
    checked = guard(b, begin, timeout, 1);
    if (checked) return retain_failure(b, checked == 2 ? K70BridgeTimeout :
                                       K70BridgeBackendFailure);
    uint32_t fence = 0;
    if (b->ops.submit_copy(b->ops.cookie, &b->source, &b->destination, &fence) ||
        !fence || fence <= b->last_fence)
        return retain_failure(b, K70BridgeBackendFailure);
    for (unsigned poll = 0; poll < budget; ++poll) {
        checked = guard(b, begin, timeout, 1);
        if (checked) return retain_failure(b, checked == 2 ? K70BridgeTimeout :
                                           K70BridgeBackendFailure);
        struct K70BridgeObservation observed = {0};
        if (b->ops.observe(b->ops.cookie, fence, &observed) ||
            observed.lease_epoch != b->epoch)
            return retain_failure(b, K70BridgeBackendFailure);
        if (observed.fault_flags || observed.completed_fence > fence)
            return retain_failure(b, K70BridgeFault);
        if (observed.completed_fence == fence) {
            checked = guard(b, begin, timeout, 1);
            if (checked) return retain_failure(b, checked == 2 ? K70BridgeTimeout :
                                               K70BridgeBackendFailure);
            if (b->ops.acquire_destination(b->ops.cookie, &b->destination))
                return retain_failure(b, K70BridgeBackendFailure);
            checked = guard(b, begin, timeout, 1);
            if (checked) return retain_failure(b, checked == 2 ? K70BridgeTimeout :
                                               K70BridgeBackendFailure);
            if (b->ops.verify_copy(b->ops.cookie, &b->source, &b->destination))
                return retain_failure(b, K70BridgeFault);
            checked = guard(b, begin, timeout, 1);
            if (checked) return retain_failure(b, checked == 2 ? K70BridgeTimeout :
                                               K70BridgeBackendFailure);
            ++b->completed_copies;
            b->last_fence = fence;
            if (b->ops.present(b->ops.cookie, &b->destination, fence))
                return retain_failure(b, K70BridgeBackendFailure);
            checked = guard(b, begin, timeout, 1);
            if (checked) return retain_failure(b, checked == 2 ? K70BridgeTimeout :
                                               K70BridgeBackendFailure);
            b->ops.release(b->ops.cookie, &b->destination);
            b->retained_destination = 0;
            checked = guard(b, begin, timeout, 1);
            if (checked) return retain_failure(b, checked == 2 ? K70BridgeTimeout :
                                               K70BridgeBackendFailure);
            b->ops.release(b->ops.cookie, &b->source);
            b->retained_source = 0;
            checked = guard(b, begin, timeout, 1);
            if (checked) return retain_failure(b, checked == 2 ? K70BridgeTimeout :
                                               K70BridgeBackendFailure);
            ++b->presented_copies;
            b->state = K70BridgeReady;
            return K70BridgeSuccess;
        }
        if (b->ops.idle(b->ops.cookie))
            return retain_failure(b, K70BridgeBackendFailure);
    }
    return retain_failure(b, K70BridgeTimeout);
}
