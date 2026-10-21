/* SPDX-License-Identifier: MIT
 * Software callback peer: tests lifecycle policy, NEVER GPU execution. */
#include "../../gpu/K70/K70GpuSystemBridge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
enum Mode { Normal, NoFence, FutureFence, Fault, EpochChange, Revoke,
    FrozenClock, BackwardsClock, SubmitFailure, AcquireFailure, PresentFailure,
    ZeroFence, OldFence, RetainFirstFailure, RetainSecondFailure, LateAcquire,
    VerifyFailure, PresentRevoke, LatePresent };
struct Peer {
    enum Mode mode;
    uint64_t time;
    unsigned retains, releases, submits, observes, acquires, presents, idles;
    struct K70BridgeReadyEvidence evidence;
};
static int ready(void *p, struct K70BridgeReadyEvidence *out)
{ *out = ((struct Peer *)p)->evidence; return 0; }
static int lease(void *p, uint64_t epoch)
{
    struct Peer *s = p;
    return epoch != 9 || (s->mode == Revoke && s->submits) ||
           (s->mode == PresentRevoke && s->presents);
}
static int now(void *p, uint64_t *out)
{
    struct Peer *s = p;
    if (s->mode == BackwardsClock && s->submits) { *out = 0; return 0; }
    if (s->mode != FrozenClock) ++s->time;
    *out = s->time;
    return 0;
}
static int retain(void *p, const struct K70BridgeSurface *surface)
{
    struct Peer *s = p;
    assert(surface->import_id == 1 || surface->import_id == 2);
    ++s->retains;
    return (s->mode == RetainFirstFailure && s->retains == 1) ||
           (s->mode == RetainSecondFailure && s->retains == 2);
}
static void release(void *p, const struct K70BridgeSurface *surface)
{ assert(surface->import_id); ++((struct Peer *)p)->releases; }
static int submit(void *p, const struct K70BridgeSurface *src,
                  const struct K70BridgeSurface *dst, uint32_t *fence)
{
    struct Peer *s = p;
    assert(s->retains == (s->submits + 1) * 2);
    assert(src->import_id != dst->import_id);
    ++s->submits;
    *fence = s->mode == ZeroFence ? 0 : s->mode == OldFence ? 3 : 3 + s->submits;
    return s->mode == SubmitFailure;
}
static int observe(void *p, uint32_t fence, struct K70BridgeObservation *out)
{
    struct Peer *s = p;
    ++s->observes;
    out->lease_epoch = s->mode == EpochChange ? 10 : 9;
    out->fault_flags = s->mode == Fault;
    out->completed_fence = (s->mode == NoFence || s->mode == FrozenClock ||
                           s->observes % 3) ? 0 : fence;
    if (s->mode == FutureFence) out->completed_fence = fence + 1;
    return 0;
}
static int idle(void *p) { ++((struct Peer *)p)->idles; return 0; }
static int acquire(void *p, const struct K70BridgeSurface *surface)
{
    struct Peer *s = p;
    assert(surface->import_id == 2 && s->observes >= 3);
    ++s->acquires;
    if (s->mode == LateAcquire) s->time += 10000;
    return s->mode == AcquireFailure;
}
static int verify(void *p, const struct K70BridgeSurface *src,
                  const struct K70BridgeSurface *dst)
{
    assert(src->import_id == 1 && dst->import_id == 2);
    assert(((struct Peer *)p)->acquires);
    return ((struct Peer *)p)->mode == VerifyFailure;
}
static int present(void *p, const struct K70BridgeSurface *surface, uint32_t fence)
{
    struct Peer *s = p;
    assert(surface->import_id == 2 && fence > 2 && s->acquires > s->presents);
    ++s->presents;
    if (s->mode == LatePresent) s->time += 10000;
    return s->mode == PresentFailure;
}
static struct K70BridgeOps ops(struct Peer *p)
{
    struct K70BridgeOps out = {p, ready, lease, now, retain, release, submit,
                              observe, idle, acquire, verify, present};
    return out;
}
static struct Peer peer(enum Mode mode)
{
    struct Peer p;
    memset(&p, 0, sizeof(p));
    p.mode = mode;
    p.evidence = (struct K70BridgeReadyEvidence){9, 3, 3, 1024, 2, 1, 1};
    return p;
}
static const struct K70BridgeSurface source = {1, 64, 0, 4, 4, 16, K70_BRIDGE_BGRA8};
static const struct K70BridgeSurface destination = {2, 96, 8, 4, 4, 20, K70_BRIDGE_BGRA8};
static void failed_run(enum Mode mode, enum K70BridgeStatus expected)
{
    struct Peer p = peer(mode);
    struct K70BridgeOps o = ops(&p);
    struct K70GpuSystemBridge b = {0};
    assert(K70GpuSystemBridgeBind(&b, &o) == K70BridgeSuccess);
    assert(K70GpuSystemBridgeCopy(&b, &source, &destination, 100, 20) == expected);
    assert(b.state == K70BridgeRetained);
    assert(b.retained_source && b.retained_destination && p.releases == 0);
    assert(b.presented_copies == 0);
    if (mode != PresentFailure && mode != PresentRevoke && mode != LatePresent)
        assert(p.presents == 0);
    unsigned calls = p.observes + p.submits + p.retains;
    assert(K70GpuSystemBridgeCopy(&b, &source, &destination, 100, 20) == K70BridgeQuarantined);
    assert(calls == p.observes + p.submits + p.retains);
}
int main(void)
{
    struct Peer p = peer(Normal);
    struct K70BridgeOps o = ops(&p);
    struct K70GpuSystemBridge b = {0};
    assert(K70GpuSystemBridgeBind(&b, &o) == K70BridgeSuccess);
    assert(K70GpuSystemBridgeBind(&b, &o) == K70BridgeNotReady);
    for (unsigned n = 1; n <= 3; ++n) {
        assert(K70GpuSystemBridgeCopy(&b, &source, &destination, 100, 20) == K70BridgeSuccess);
        assert(b.completed_copies == n && b.presented_copies == n);
        assert(p.releases == 2 * n && b.last_fence == 3 + n);
        assert(!b.retained_source && !b.retained_destination);
    }
    const enum Mode modes[] = {NoFence, FutureFence, Fault, EpochChange, Revoke,
        FrozenClock, BackwardsClock, SubmitFailure, AcquireFailure, PresentFailure,
        ZeroFence, OldFence, LateAcquire, VerifyFailure, PresentRevoke, LatePresent};
    const enum K70BridgeStatus statuses[] = {K70BridgeTimeout, K70BridgeFault,
        K70BridgeFault, K70BridgeBackendFailure, K70BridgeBackendFailure,
        K70BridgeTimeout, K70BridgeBackendFailure, K70BridgeBackendFailure,
        K70BridgeBackendFailure, K70BridgeBackendFailure, K70BridgeBackendFailure,
        K70BridgeBackendFailure, K70BridgeTimeout, K70BridgeFault,
        K70BridgeBackendFailure, K70BridgeTimeout};
    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i)
        failed_run(modes[i], statuses[i]);
    for (unsigned field = 0; field < 8; ++field) {
        struct Peer bad = peer(Normal);
        if (field == 0) bad.evidence.native_complete = 0;
        if (field == 1) bad.evidence.observed_fence = 0;
        if (field == 2) bad.evidence.matched_words = 1023;
        if (field == 3) bad.evidence.secure_fence = 1;
        if (field == 4) bad.evidence.lease_epoch = 0;
        if (field == 5) bad.evidence.submitted_fence = 0;
        if (field == 6) bad.evidence.initialization_fence = 0;
        if (field == 7) bad.evidence.secure_fence = 4;
        struct K70BridgeOps badops = ops(&bad);
        struct K70GpuSystemBridge fresh = {0};
        assert(K70GpuSystemBridgeBind(&fresh, &badops) == K70BridgeNotReady);
        assert(!bad.retains && !bad.submits);
    }
    for (unsigned field = 0; field < 11; ++field) {
        struct K70BridgeSurface s = source;
        if (field == 0) s.import_id = 0;
        if (field == 1) s.import_id = destination.import_id;
        if (field == 2) s.allocation_bytes = UINT64_MAX;
        if (field == 3) s.offset = UINT64_MAX;
        if (field == 4) s.width = UINT32_MAX;
        if (field == 5) s.height = 0;
        if (field == 6) s.row_bytes = 15;
        if (field == 7) s.format = 0;
        if (field == 8) s.allocation_bytes = 63;
        if (field == 9) s.height = 3;
        if (field == 10) s.offset = 1;
        unsigned before = p.retains;
        assert(K70GpuSystemBridgeCopy(&b, &s, &destination, 100, 20) == K70BridgeBadArgument);
        assert(p.retains == before);
    }
    for (enum Mode m = RetainFirstFailure; m <= RetainSecondFailure; ++m) {
        struct Peer bad = peer(m);
        struct K70BridgeOps badops = ops(&bad);
        struct K70GpuSystemBridge fresh = {0};
        assert(K70GpuSystemBridgeBind(&fresh, &badops) == K70BridgeSuccess);
        assert(K70GpuSystemBridgeCopy(&fresh, &source, &destination, 100, 20) == K70BridgeBackendFailure);
        assert(fresh.state == K70BridgeReady && !bad.submits);
        assert(bad.releases == (m == RetainSecondFailure ? 1u : 0u));
    }
    struct K70GpuSystemBridge fresh = {0};
    struct K70BridgeOps missing = o;
    missing.present = 0;
    assert(K70GpuSystemBridgeBind(&fresh, &missing) == K70BridgeBadArgument);
    assert(K70GpuSystemBridgeCopy(&fresh, &source, &destination, 100, 20) == K70BridgeNotReady);
    assert(K70GpuSystemBridgeCopy(&b, &source, &destination, 0, 20) == K70BridgeBadArgument);
    assert(K70GpuSystemBridgeCopy(&b, &source, &destination, 100, 0) == K70BridgeBadArgument);
    puts("K70 system bridge lifecycle tests passed (software peer only; no GPU/Metal claim)");
    return 0;
}
