/* Read-only SMEM snapshot decoder for APSS(0)/MPSS(1). No live pointer access,
 * allocation, locks or writes. The caller must supply a stable private copy.
 * Uncached private items only; cached items are reported as unsupported.
 */
#ifndef VERMEER_BASEBAND_SMEM_H
#define VERMEER_BASEBAND_SMEM_H
#include "vermeer_baseband_protocol.h"
enum { VBB_SMEM_OK=0,VBB_SMEM_BAD=1,VBB_SMEM_NO_PARTITION=2,VBB_SMEM_CACHED=3 };
struct vbb_smem_item { size_t offset,size; };
struct vbb_smem {
    size_t partition_offset,partition_size;
    uint32_t entries;
    struct vbb_smem_item descriptor,tx,rx;
};
static inline int vbb_smem_snapshot(const uint8_t *p,size_t n,struct vbb_smem *out)
{
    struct vbb_smem v={0};size_t partition=0,psize=0;unsigned matched=0;
    if(!out)return VBB_SMEM_BAD;
    *out=v;
    if(!p || n<8192 || n>16u*1024u*1024u)return VBB_SMEM_BAD;
    size_t toc=n-4096;
    if(vbb_u32(p+toc)!=UINT32_C(0x434f5424) || vbb_u32(p+toc+4)!=1)return VBB_SMEM_BAD;
    uint32_t count=vbb_u32(p+toc+8);
    if(!count || count>(4096-32)/48)return VBB_SMEM_BAD;
    for(uint32_t i=0;i<count;i++) {
        const uint8_t *e=p+toc+32+i*48;
        size_t off=vbb_u32(e),size=vbb_u32(e+4);
        if(!size)continue;
        if(off<32 || off%8 || size<32 || off>toc || size>toc-off)return VBB_SMEM_BAD;
        for(uint32_t j=0;j<i;j++) {
            const uint8_t *other=p+toc+32+j*48;
            size_t a=vbb_u32(other),b=vbb_u32(other+4);
            if(b && off<a+b && a<off+size)return VBB_SMEM_BAD;
        }
        uint16_t host0=vbb_u16(e+12),host1=vbb_u16(e+14);
        if(!((host0==0 && host1==1)||(host0==1 && host1==0)))continue;
        if(++matched!=1)return VBB_SMEM_BAD;
        if(vbb_u32(p+off)!=UINT32_C(0x54525024) || vbb_u16(p+off+4)!=host0 ||
           vbb_u16(p+off+6)!=host1 || vbb_u32(p+off+8)!=size)return VBB_SMEM_BAD;
        partition=off;psize=size;
    }
    if(!matched)return VBB_SMEM_NO_PARTITION;
    size_t uncached=vbb_u32(p+partition+12),cached=vbb_u32(p+partition+16);
    if(uncached<32 || uncached%8 || uncached>cached || cached>psize)return VBB_SMEM_BAD;
    v.partition_offset=partition;v.partition_size=psize;
    for(size_t at=32;at<uncached;) {
        if(uncached-at<16 || ++v.entries>4096)return VBB_SMEM_BAD;
        const uint8_t *e=p+partition+at;
        size_t size=vbb_u32(e+4),data_pad=vbb_u16(e+8),header_pad=vbb_u16(e+10);
        if(vbb_u16(e)!=0xa5a5 || !size || size%8 || header_pad%8 || data_pad>=size ||
           header_pad>uncached-at-16 || size>uncached-at-16-header_pad)return VBB_SMEM_BAD;
        unsigned id=vbb_u16(e+2);
        struct vbb_smem_item *item=id==478?&v.descriptor:id==479?&v.tx:id==480?&v.rx:NULL;
        if(item) {
            if(item->size)return VBB_SMEM_BAD;
            item->offset=partition+at+16+header_pad;item->size=size-data_pad;
        }
        at+=16+header_pad+size;
    }
    *out=v;
    return cached!=psize?VBB_SMEM_CACHED:VBB_SMEM_OK;
}
#endif
