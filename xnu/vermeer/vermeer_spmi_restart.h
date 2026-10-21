/* SPDX-License-Identifier: MIT
 * Default-off K70 SDAM restart-cell backend candidate. No mappings, kernel
 * registration, enable boot argument, reset instruction or USB entry point.
 * Register/bit facts reviewed from pinned MiCode53ef33eacdc4f7ac737451e4cfc91963d59e3956.
 * Native binding still needs observed arbiter version/APID ownership, safe
 * access/serialization and evidence of the boot firmware's reason consumer.
 */
#ifndef VERMEER_SPMI_RESTART_H
#define VERMEER_SPMI_RESTART_H
#include <stdint.h>
#include <stddef.h>
enum { VRS_CORE,VRS_CONFIG,VRS_OBSERVER,VRS_CHANNEL,VRS_BANKS };
enum { VRS_OK=1,VRS_DISABLED=-1,VRS_IO=-2,VRS_LAYOUT=-3,VRS_OWNER=-4,
       VRS_DENIED=-5,VRS_TIMEOUT=-6,VRS_READBACK=-7,VRS_RETIRED=-8 };
struct vrs_region {uint64_t base;uint32_t bytes;};
static const struct vrs_region vrs_k70_regions[VRS_BANKS]={
    {UINT64_C(0x0c400000),0x3000},{UINT64_C(0x0c42d000),0x4000},
    {UINT64_C(0x0c440000),0x80000},{UINT64_C(0x0c500000),0x400000}
};
struct vrs_backend {
    /* Platform owner only. Zero-initialization never permits even an MMIO
     * read. This is not a proof of access: no live native binder exists yet. */
    unsigned enabled,retired;
    void *ctx;
    struct vrs_region regions[VRS_BANKS];
    int (*read32)(void *,unsigned,unsigned,uint32_t *);
    int (*write32)(void *,unsigned,unsigned,uint32_t);
    int (*pause_us)(void *); /* bounded1us, 1 success */
    uint32_t version,features,apid,owner,map,status;
    unsigned old_byte,new_byte,reason_write_started;
    int error;
};
static inline int vrs_fail(struct vrs_backend *b,int error)
{b->retired=1;b->error=error;return error;}
static inline int vrs_read(struct vrs_backend *b,unsigned bank,unsigned off,uint32_t *out)
{
    if(!b || !b->enabled)return VRS_DISABLED;
    if(b->retired)return VRS_RETIRED;
    if(!b->read32)return vrs_fail(b,VRS_IO);
    if(bank>=VRS_BANKS || !out || (off&3) || b->regions[bank].bytes<4 ||
       off>b->regions[bank].bytes-4 || b->read32(b->ctx,bank,off,out)!=1)
        return vrs_fail(b,VRS_IO);
    return VRS_OK;
}
static inline int vrs_write(struct vrs_backend *b,unsigned bank,unsigned off,uint32_t value)
{
    if(!b || !b->enabled)return VRS_DISABLED;
    if(b->retired)return VRS_RETIRED;
    if(!b->write32)return vrs_fail(b,VRS_IO);
    unsigned base=b->apid*(bank==VRS_CHANNEL?0x1000u:0x20u);
    unsigned allowed=(bank==VRS_OBSERVER && off==base &&
        (value==((1u<<27)|0x440u) || value==((1u<<27)|0x480u))) ||
        (bank==VRS_CHANNEL && ((off==base && value==0x480u) ||
         (off==base+0x10 && value<=255 && (value&0xfeu)==4u)));
    if(!allowed || bank>=VRS_BANKS || (off&3) || b->regions[bank].bytes<4 ||
       off>b->regions[bank].bytes-4 || b->write32(b->ctx,bank,off,value)!=1)
        return vrs_fail(b,VRS_IO);
    return VRS_OK;
}
static inline int vrs_probe(struct vrs_backend *b)
{
    if(!b || !b->enabled)return VRS_DISABLED;
    if(b->retired)return VRS_RETIRED;
    if(!b->read32 || !b->write32 || !b->pause_us)return vrs_fail(b,VRS_IO);
    for(unsigned i=0;i<VRS_BANKS;i++)
        if(b->regions[i].base!=vrs_k70_regions[i].base || b->regions[i].bytes!=vrs_k70_regions[i].bytes)
            return vrs_fail(b,VRS_LAYOUT);
    if(vrs_read(b,VRS_CORE,0,&b->version)!=1 || vrs_read(b,VRS_CORE,4,&b->features)!=1)return b->error;
    /* Explicit v7 candidate only, not copied v5 channel geometry. Actual K70
     * value is still unknown and must be observed without enabling writes. */
    unsigned count=b->features&0x7ffu;
    if(b->version<0x70000000u || b->version>=0x80000000u || !count || count>1024)
        return vrs_fail(b,VRS_LAYOUT);
    unsigned owned=0;
    for(unsigned i=0;i<count;i++) {
        uint32_t map,owner;
        if(vrs_read(b,VRS_CORE,0x2000+4*i,&map)!=1)return b->error;
        if(!map || ((map>>8)&0xfffu)!=0x71u)continue; /* SID0/peripheral71 only */
        if(vrs_read(b,VRS_CONFIG,4*i,&owner)!=1)return b->error;
        if((owner&7u)!=0)continue; /* saved primary bus0, APSS EE0 */
        b->apid=i;b->map=map;b->owner=owner;owned++;
    }
    if(owned!=1)return vrs_fail(b,VRS_OWNER);
    return VRS_OK;
}
static inline int vrs_byte(struct vrs_backend *b,unsigned address,unsigned write,uint8_t *value)
{
    if(!value || (address!=0x44 && address!=0x48) || (write && address!=0x48))
        return b?vrs_fail(b,VRS_LAYOUT):VRS_DISABLED;
    int result=vrs_probe(b);if(result!=1)return result;
    unsigned bank=write?VRS_CHANNEL:VRS_OBSERVER;
    unsigned off=b->apid*(write?0x1000u:0x20u); /* bus0/EE0 only */
    if(write && vrs_write(b,bank,off+0x10,*value)!=1)return b->error;
    /* v2+ format uses only the low address byte: the APID selects SID/PPID.
     * EXT_WRITEL=0, EXT_READL=1, byte count minus one=0. */
    if(write)b->reason_write_started=1;
    if(vrs_write(b,bank,off,(write?0u:1u<<27)|(address<<4))!=1)return b->error;
    for(unsigned i=0;i<1000;i++) {
        if(vrs_read(b,bank,off+8,&b->status)!=1)return b->error;
        if(b->status&0x0eu)return vrs_fail(b,VRS_DENIED);
        if(b->status&1u) {
            if(!write) {
                uint32_t data;
                if(vrs_read(b,bank,off+0x18,&data)!=1)return b->error;
                *value=(uint8_t)data;
            }
            return VRS_OK;
        }
        if(b->pause_us(b->ctx)!=1)return vrs_fail(b,VRS_IO);
    }
    return vrs_fail(b,VRS_TIMEOUT);
}
static inline int vrs_program_bootloader(struct vrs_backend *b)
{
    if(!b || !b->enabled)return VRS_DISABLED;
    if(b->retired)return VRS_RETIRED;
    uint8_t size=0,old=0,check=0;
    int result=vrs_byte(b,0x44,0,&size);if(result!=1)return result;
    /* Official provider: SDAM size byte *32, memory starts40. Cell48 must
     * be in memory and outside the provider's read-only40/41/44 registers. */
    if(!size)return vrs_fail(b,VRS_LAYOUT);
    result=vrs_byte(b,0x48,0,&old);if(result!=1)return result;
    b->old_byte=old;
    uint8_t replacement=(uint8_t)((old&1u)|(2u<<1)); /* cell bits1..7, logical2 */
    b->new_byte=replacement;
    result=vrs_byte(b,0x48,1,&replacement);if(result!=1)return result;
    result=vrs_byte(b,0x48,0,&check);if(result!=1)return result;
    if(check!=replacement)return vrs_fail(b,VRS_READBACK);
    b->error=VRS_OK;b->retired=1;return VRS_OK; /* one reason-program attempt */
}
#endif
