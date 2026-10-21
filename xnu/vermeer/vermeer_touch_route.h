/* SPDX-License-Identifier: MIT
 * Read-only K70 apps-SMMU/GENI board policy. No translation changes.
 * Geometry is the exact legacy SMMUv2 layout already observed by UFS/USB.
 * The saved K70 wrapper1 DT selects SID0xa3, not UFS0x60 or USB0x40.
 */
#ifndef VERMEER_TOUCH_ROUTE_H
#define VERMEER_TOUCH_ROUTE_H
#include <stdint.h>
#include <stddef.h>
enum { VTR_OK, VTR_LAYOUT, VTR_MATCH, VTR_CONTEXT, VTR_TRANSLATED, VTR_UNSAMPLED };
struct vtr_route {
    uint32_t id0,id1,id2,cr0,matches,slot,smr,s2cr,cbar,sctlr,reason;
};
static inline int vtr_reset_high(uint32_t config,uint32_t io)
{
    /* Mux must be GPIO; sample the pad, not just its output latch. For an
     * output, both the programmed latch and observed pad must be high. */
    return !(config&0x3cu) && (io&1u) && (!(config&0x200u) || (io&2u));
}
static inline int vtr_dma_range(uint64_t pa,size_t bytes,size_t page)
{
    return page==16384 && bytes==2*page && !(pa&(page-1)) &&
        pa>=UINT64_C(0x8c0000000) && pa<UINT64_C(0x940000000) &&
        bytes<=UINT64_C(0x940000000)-pa;
}
static inline int vtr_may_restore(uint32_t inherited_dma,unsigned quarantined,uint32_t status)
{
    /* Even M_ACTIVE=0 does not authorize changing an unowned DMA engine. */
    return !(inherited_dma&1u) && !quarantined && !(status&0x1001u);
}
static inline int vtr_identity(void *ctx,uint32_t (*read)(void *,unsigned),struct vtr_route *out)
{
    if(!out || !read)return 0;
    *out=(struct vtr_route){0};out->reason=VTR_LAYOUT;
    out->id0=read(ctx,0x20);out->id1=read(ctx,0x24);out->id2=read(ctx,0x28);out->cr0=read(ctx,0);
    if(out->id0!=0x4c017e69u || out->id1!=0x60000046u || out->id2!=0x5111u ||
       out->cr0!=0x00280406u)return 0;
    for(unsigned i=0;i<105;i++) {
        uint32_t smr=read(ctx,0x800+4*i);
        if((smr&0x80000000u) && !((smr^0xa3u)&~((smr>>16)&0x7fffu)&0xffffu)) {
            out->matches++;out->slot=i;out->smr=smr;out->s2cr=read(ctx,0xc00+4*i);
        }
    }
    out->reason=VTR_MATCH;
    if(out->matches!=1)return 0;
    unsigned type=(out->s2cr>>16)&3u;
    out->reason=VTR_TRANSLATED;
    if(type==1) {out->reason=VTR_OK;return 1;} /* existing bypass, no writes */
    if(type!=0)return 0;
    unsigned bank=out->s2cr&255u;out->reason=VTR_CONTEXT;
    if(bank>=70)return 0;
    out->cbar=read(ctx,0x1000+4*bank);out->sctlr=read(ctx,0x80000+0x1000*bank);
    out->reason=VTR_TRANSLATED;
    if(((out->cbar>>16)&3u)!=1 || (out->sctlr&1u))return 0;
    out->reason=VTR_OK;return 1;
}
#endif
