/* Caller supplies a nonblocking lock; no allocations or filesystem I/O. */
#ifndef VERMEER_LOG_RING_H
#define VERMEER_LOG_RING_H
#ifdef VERMEER_HOST_TEST
#include <stdint.h>
#else
#include <sys/types.h>
#endif
#ifndef VL_RING_SIZE
#define VL_RING_SIZE (1u << 20)
#endif
struct vl_ring { uint64_t total; char bytes[VL_RING_SIZE]; };
static inline void vl_append(struct vl_ring *r, const char *s, unsigned n)
{
    uint64_t at=r->total;
    r->total+=n;
    if (n>VL_RING_SIZE) { unsigned skip=n-VL_RING_SIZE; s+=skip; at+=skip; n=VL_RING_SIZE; }
    for (unsigned i=0;i<n;i++) r->bytes[(at+i)%VL_RING_SIZE]=s[i];
}
static inline unsigned vl_copy(const struct vl_ring *r, uint64_t *pos, char *out, unsigned max)
{
    if (*pos>r->total) *pos=0;
    if (r->total-*pos>VL_RING_SIZE) *pos=r->total-VL_RING_SIZE;
    uint64_t left=r->total-*pos;
    unsigned n=left<max?(unsigned)left:max;
    for (unsigned i=0;i<n;i++) out[i]=r->bytes[(*pos+i)%VL_RING_SIZE];
    *pos+=n;
    return n;
}
#endif
