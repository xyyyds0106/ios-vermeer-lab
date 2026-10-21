/* Fixed-size frame ownership and clipped BGRA copies for the K70 panel.
 * No allocation or callbacks under the framebuffer lock. The caller retains
 * incoming surfaces before replace, and releases displaced/taken refs outside
 * that lock. A pending frame therefore survives userspace surface release.
 */
#ifndef VERMEER_FRAME_POLICY_H
#define VERMEER_FRAME_POLICY_H
#ifdef VERMEER_HOST_TEST
#include <stdint.h>
#else
#include <sys/types.h>
#endif
#define VFRAME_LAYERS 4
/* Monotonic nanoseconds, independent of timer callback count. hz=0 keeps
 * the existing unthrottled (60 Hz) request. Budget at least the configured
 * interval and twice the last copy cost so slow CPU copies retain headroom.
 */
static inline uint64_t vframe_interval_ns(uint32_t hz)
{
    if(!hz || hz>60)hz=60;
    return (1000000000ULL+hz-1)/hz;
}
static inline uint64_t vframe_next_ns(uint64_t now,uint64_t copy_ns,uint64_t interval)
{
    const uint64_t limit=~(uint64_t)0;
    uint64_t budget=copy_ns>limit/2?limit:copy_ns*2;
    if(budget<interval)budget=interval;
    return budget>limit-now?limit:now+budget;
}
struct vframe_refs { void *slot[VFRAME_LAYERS]; };
static inline void vframe_take(struct vframe_refs *pending,struct vframe_refs *out)
{
    for (unsigned i=0;i<VFRAME_LAYERS;i++) {
        out->slot[i]=pending->slot[i];pending->slot[i]=0;
    }
}
static inline void vframe_replace(struct vframe_refs *pending,struct vframe_refs *incoming,
                                  struct vframe_refs *displaced)
{
    vframe_take(pending,displaced);vframe_take(incoming,pending);
}
struct vframe_copy { uint64_t src,dst;uint32_t bytes,rows; };
static inline int vframe_clip(struct vframe_copy *out,int64_t sx,int64_t sy,
    int64_t dx,int64_t dy,int64_t w,int64_t h,uint32_t sw,uint32_t sh,uint32_t stride,
    uint64_t offset,uint64_t length,uint32_t dw,uint32_t dh,uint32_t dstride,
    uint64_t dlength,int32_t origin_x,int32_t origin_y)
{
    if (!sw || !sh || !dw || !dh || (uint64_t)sw*4>stride || (uint64_t)dw*4>dstride ||
        offset>length || w<=0 || h<=0) return 0;
    /* Incoming geometry is signed32; reject larger synthetic/corrupt values
     * before translating or negating, avoiding all signed overflow paths.
     */
    const int64_t lo=(-2147483647LL-1),hi=2147483647LL;
    if (sx<lo || sx>hi || sy<lo || sy>hi || dx<lo || dx>hi || dy<lo || dy>hi ||
        w>hi || h>hi) return 0;
    dx+=origin_x;dy+=origin_y;
    if (sx<0) { dx-=sx;w+=sx;sx=0; }
    if (sy<0) { dy-=sy;h+=sy;sy=0; }
    if (dx<0) { sx-=dx;w+=dx;dx=0; }
    if (dy<0) { sy-=dy;h+=dy;dy=0; }
    if (w<=0 || h<=0 || sx>=sw || sy>=sh || dx>=dw || dy>=dh) return 0;
    if (w>(int64_t)sw-sx) w=sw-sx;
    if (h>(int64_t)sh-sy) h=sh-sy;
    if (w>(int64_t)dw-dx) w=dw-dx;
    if (h>(int64_t)dh-dy) h=dh-dy;
    if (w<=0 || h<=0) return 0;
    uint64_t remaining=length-offset,send=(uint64_t)(sx+w)*4,dend=(uint64_t)(dx+w)*4;
    if (send>remaining || (uint64_t)(sy+h-1)>(remaining-send)/stride ||
        dend>dlength || (uint64_t)(dy+h-1)>(dlength-dend)/dstride) return 0;
    out->src=offset+(uint64_t)sy*stride+(uint64_t)sx*4;
    out->dst=(uint64_t)dy*dstride+(uint64_t)dx*4;
    out->bytes=(uint32_t)w*4;out->rows=(uint32_t)h;
    return 1;
}
#endif
