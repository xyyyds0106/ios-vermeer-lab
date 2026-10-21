/* SPDX-License-Identifier: MIT
 * Narrow apps-SMMUv2 SIDb6 attachment for an exclusively quiesced GPI provider.
 * Uses the real ArmSmmu4K table/context encoders; no global reset or bypass.
 * All tables, mappings, clocks and leases are retained until reboot.
 */
#ifndef VERMEER_TOUCH_IOMMU_H
#define VERMEER_TOUCH_IOMMU_H
#include "ArmSmmu4KContext.h"
struct vti_ops {
    void *ctx;
    int (*guard)(void *); /* real exclusive provider + quiescence + live clocks */
    int (*read32)(void *,unsigned,uint32_t *);
    int (*read64)(void *,unsigned,uint64_t *);
    int (*write32)(void *,unsigned,uint32_t);
    int (*write64)(void *,unsigned,uint64_t);
    int (*clean_tables)(void *,void *,size_t);
    int (*now_us)(void *,uint64_t *);
    int (*pause_us)(void *,unsigned);
};
enum {VTI_OK,VTI_ARGUMENT,VTI_LEASE,VTI_IO,VTI_LAYOUT,VTI_BUSY,VTI_TABLES,VTI_TIMEOUT,VTI_READBACK};
enum {VTI_CONFLICT_NONE,VTI_CONFLICT_SID_ALIAS,VTI_CONFLICT_FOREIGN_SID,
      VTI_CONFLICT_NO_SLOT,VTI_CONFLICT_NO_BANK,VTI_CONFLICT_NO_ASID,
      VTI_CONFLICT_SMR_CHANGED,VTI_CONFLICT_S2CR_CHANGED,VTI_CONFLICT_BANK_CHANGED};
struct vti {
    struct vti_ops io;uint64_t physical[5],table_pa;void *table_cpu;
    ArmSmmu4KConfig config;ArmSmmu4KResult tables;ArmSmmu4KContextConfig context;
    ArmSmmu4KContextImage image;
    unsigned slot,bank,asid,error,touched,attached,retired;uint32_t old_smr,old_s2cr,bank_control,bank_actlr;
    uint64_t begin,last;
    /* Retained observations only; printing failures never adds device I/O. */
    unsigned scan_phase,scan_index,conflict,valid_routes,enabled_banks,reserved_banks,unsafe_actlr_banks;
    unsigned first_unsafe_bank;uint32_t first_unsafe_actlr,conflict_smr,conflict_s2cr;
};
static inline int vti_fail(struct vti *v,unsigned error)
{v->error=error;v->retired=1;return 0;}
static inline int vti_busy(struct vti *v,unsigned conflict)
{v->conflict=conflict;return vti_fail(v,VTI_BUSY);}
static inline int vti_guard(struct vti *v)
{
    uint64_t now;
    if(v->retired || v->io.guard(v->io.ctx)!=1)return vti_fail(v,VTI_LEASE);
    if(v->io.now_us(v->io.ctx,&now)!=1 || now<v->last || now-v->begin>=1000000)return vti_fail(v,VTI_TIMEOUT);
    v->last=now;return 1;
}
static inline int vti_rd(struct vti *v,unsigned off,uint32_t *out)
{if(!vti_guard(v))return 0;if(v->io.read32(v->io.ctx,off,out)!=1)return vti_fail(v,VTI_IO);return vti_guard(v);}
static inline int vti_r64(struct vti *v,unsigned off,uint64_t *out)
{if(!vti_guard(v))return 0;if(v->io.read64(v->io.ctx,off,out)!=1)return vti_fail(v,VTI_IO);return vti_guard(v);}
static inline int vti_wr(struct vti *v,unsigned off,uint32_t value)
{if(!vti_guard(v))return 0;v->touched=1;if(v->io.write32(v->io.ctx,off,value)!=1)return vti_fail(v,VTI_IO);return vti_guard(v);}
static inline int vti_w64(struct vti *v,unsigned off,uint64_t value)
{if(!vti_guard(v))return 0;v->touched=1;if(v->io.write64(v->io.ctx,off,value)!=1)return vti_fail(v,VTI_IO);return vti_guard(v);}
static inline int vti_prepare(struct vti *v)
{
    if(!v || v->retired || v->attached || v->touched || !v->io.guard || !v->io.read32 || !v->io.read64 ||
       !v->io.write32 || !v->io.write64 || !v->io.clean_tables || !v->io.now_us || !v->io.pause_us ||
       !v->table_cpu || ((uintptr_t)v->table_cpu&16383) || (v->table_pa&16383) ||
       v->table_pa<UINT64_C(0x8c0000000) || v->table_pa>UINT64_C(0x940000000)-16384)return 0;
    if(v->io.now_us(v->io.ctx,&v->last)!=1)return vti_fail(v,VTI_TIMEOUT);
    v->begin=v->last;
    v->scan_phase=1;
    uint32_t id0,id1,id2,cr0,global;
    if(!vti_rd(v,0x20,&id0) || !vti_rd(v,0x24,&id1) || !vti_rd(v,0x28,&id2) ||
       !vti_rd(v,0,&cr0) || !vti_rd(v,0x48,&global))return 0;
    if(id0!=0x4c017e69 || id1!=0x60000046 || id2!=0x5111 || cr0!=0x280406 || global)
        return vti_fail(v,VTI_LAYOUT);
    unsigned used[70]={0},taken_asid[256]={0},matches=0,free_slot=105;v->slot=105;v->bank=70;
    for(unsigned i=0;i<105;i++) {
        v->scan_phase=2;v->scan_index=i;
        uint32_t smr,s2cr;
        if(!vti_rd(v,0x800+4*i,&smr) || !vti_rd(v,0xc00+4*i,&s2cr))return 0;
        if(!(smr&0x80000000u)) {if(free_slot==105)free_slot=i;continue;}
        ++v->valid_routes;
        if(!(s2cr&0x30000) && (s2cr&255)<70)used[s2cr&255]=1;
        if(!((smr^0xb6u)&~((smr>>16)&0x7fffu)&0x7fffu)) {
            v->conflict_smr=smr;v->conflict_s2cr=s2cr;
            if(smr!=0x800000b6u || ++matches>1)return vti_busy(v,VTI_CONFLICT_SID_ALIAS);
            /* An enabled foreign translation can serve firmware outside this
             * Apps-EE DT mask. Without its owner we cannot redirect it.
             * Existing identity is reused by the native adapter; new mappings
             * here attach ONLY an unbound or already faulting exact SID. */
            if(((s2cr>>16)&3)!=2)return vti_busy(v,VTI_CONFLICT_FOREIGN_SID);
            v->slot=i;v->old_smr=smr;v->old_s2cr=s2cr;
        }
    }
    if(!matches) {
        if(free_slot==105)return vti_busy(v,VTI_CONFLICT_NO_SLOT);
        v->slot=free_slot;
        if(!vti_rd(v,0x800+4*v->slot,&v->old_smr) || !vti_rd(v,0xc00+4*v->slot,&v->old_s2cr))return 0;
    }
    for(unsigned i=0;i<70;i++) {
        v->scan_phase=3;v->scan_index=i;
        uint32_t control,actlr;uint64_t ttbr;
        if(!vti_rd(v,0x80000+4096*i,&control))return 0;
        if(control&1) {
            ++v->enabled_banks;
            if(!vti_r64(v,0x80020+4096*i,&ttbr))return 0;
            unsigned asid=(unsigned)(ttbr>>48);if(asid<256)taken_asid[asid]=1;
            if(!vti_r64(v,0x80028+4096*i,&ttbr))return 0;
            asid=(unsigned)(ttbr>>48);if(asid<256)taken_asid[asid]=1;
        } else if(!used[i]) {
            if(!vti_rd(v,0x80004+4096*i,&actlr))return 0;
            /* MMU-500 reset ACTLR bits 0/1 are compatible. The vendor Linux
             * arm_mmu500_reset clears only CPRE (bit 1) for errata 841119 and
             * 826419. Unknown Qualcomm configuration remains ineligible. */
            if(!(actlr&~3u)){v->bank=i;v->bank_control=control;v->bank_actlr=actlr;}
            else {
                if(!v->unsafe_actlr_banks){v->first_unsafe_bank=i;v->first_unsafe_actlr=actlr;}
                ++v->unsafe_actlr_banks;
            }
        } else {
            ++v->reserved_banks;
        }
    }
    if(v->bank==70)return vti_busy(v,VTI_CONFLICT_NO_BANK);
    v->scan_phase=4;
    for(v->asid=1;v->asid<256 && taken_asid[v->asid];v->asid++);
    if(v->asid==256)return vti_busy(v,VTI_CONFLICT_NO_ASID);
    v->scan_phase=5;
    ArmSmmu4KRange ranges[5];
    for(unsigned i=0;i<5;i++) {
        if((v->physical[i]&16383) || v->physical[i]<UINT64_C(0x8c0000000) ||
           v->physical[i]>UINT64_C(0x940000000)-16384)return vti_fail(v,VTI_ARGUMENT);
        if(v->physical[i]==v->table_pa)return vti_fail(v,VTI_ARGUMENT);
        for(unsigned j=0;j<i;j++)if(v->physical[i]==v->physical[j])return vti_fail(v,VTI_ARGUMENT);
        ranges[i]=(ArmSmmu4KRange){0x100000+16384*i,v->physical[i],16384,
            ArmSmmu4KRead|((i==1 || i==4)?ArmSmmu4KWrite:0),0,2};
    }
    v->config=(ArmSmmu4KConfig){32,36,v->table_pa,20};
    if(ArmSmmu4KBuild(&v->config,ranges,5,v->table_cpu,4,&v->tables)!=ArmSmmu4KSuccess)
        return vti_fail(v,VTI_TABLES);
    v->context=(ArmSmmu4KContextConfig){.id0=id0,.id2=id2,.lower=&v->config,.lower_tables=&v->tables,
        .asid=v->asid,.coherent_walk=0,.stall_on_fault=0,.continue_after_fault=0};
    if(ArmSmmu4KEncodeContext(&v->context,&v->image)!=ArmSmmu4KSuccess)return vti_fail(v,VTI_TABLES);
    /* Faults terminate the owned DMA and are polled before further MMIO.
     * No unhandled context interrupt is enabled or generic fault ISR faked. */
    v->image.sctlr&=~0x40u;
    if(!vti_guard(v) || v->io.clean_tables(v->io.ctx,v->table_cpu,16384)!=1)return vti_fail(v,VTI_TABLES);
    if(!vti_guard(v))return 0;
    uint32_t value;
    if(!vti_rd(v,0x800+4*v->slot,&value))return 0;
    if(value!=v->old_smr)return vti_busy(v,VTI_CONFLICT_SMR_CHANGED);
    if(!vti_rd(v,0xc00+4*v->slot,&value))return 0;
    if(value!=v->old_s2cr)return vti_busy(v,VTI_CONFLICT_S2CR_CHANGED);
    unsigned cb=0x80000+v->bank*4096;
    if(!vti_rd(v,cb,&value))return 0;
    if(value!=v->bank_control)return vti_busy(v,VTI_CONFLICT_BANK_CHANGED);
    if(!vti_rd(v,cb+4,&value))return 0;
    if(value!=v->bank_actlr)return vti_busy(v,VTI_CONFLICT_BANK_CHANGED);
    v->scan_phase=6;
    /* Only SIDb6's exclusively leased slot is faulted, never another route. */
    if(!vti_wr(v,0xc00+4*v->slot,0x20000))return 0;
    if(!vti_rd(v,0xc00+4*v->slot,&value))return 0;
    if(value!=0x20000)return vti_fail(v,VTI_READBACK);
    /* This bank alone is owned. Keep bit 0; never change global cache locks
     * or another bank. A locked ACTLR fails readback before publication. */
    if(!vti_wr(v,cb,0) || !vti_wr(v,cb+4,v->bank_actlr&~2u) ||
       !vti_wr(v,0x1800+4*v->bank,v->image.cba2r) ||
       !vti_wr(v,0x1000+4*v->bank,v->image.cbar) || !vti_wr(v,cb+0x10,v->image.tcr2) ||
       !vti_wr(v,cb+0x30,v->image.tcr) || !vti_w64(v,cb+0x20,v->image.ttbr0) ||
       !vti_w64(v,cb+0x28,v->image.ttbr1) || !vti_wr(v,cb+0x38,v->image.mair0) ||
       !vti_wr(v,cb+0x3c,v->image.mair1) || !vti_wr(v,cb+0x58,UINT32_MAX) ||
       !vti_wr(v,cb+0x618,0) || !vti_wr(v,cb+0x7f0,UINT32_MAX))return 0;
    unsigned synced=0;
    for(unsigned i=0;i<1000;i++) {
        if(!vti_rd(v,cb+0x7f4,&value))return 0;
        if(!(value&1)){synced=1;break;}
        if(v->io.pause_us(v->io.ctx,20)!=1 || !vti_guard(v))return vti_fail(v,VTI_TIMEOUT);
    }
    if(!synced)return vti_fail(v,VTI_TIMEOUT);
    const unsigned registers[][2]={{0x1800+4*v->bank,v->image.cba2r},{0x1000+4*v->bank,v->image.cbar},
        {cb+4,v->bank_actlr&~2u},{cb+0x10,v->image.tcr2},{cb+0x30,v->image.tcr},
        {cb+0x38,v->image.mair0},{cb+0x3c,0},{cb,0}};
    for(unsigned i=0;i<sizeof(registers)/sizeof(registers[0]);i++) {
        if(!vti_rd(v,registers[i][0],&value))return 0;
        /* Architectural read-as-set TBI bits do not affect the low IOVA pool. */
        unsigned mask=registers[i][0]==cb+0x10?~0x60u:UINT32_MAX;
        if((value&mask)!=(registers[i][1]&mask))return vti_fail(v,VTI_READBACK);
    }
    uint64_t wide;
    if(!vti_r64(v,cb+0x20,&wide))return 0;
    if(wide!=v->image.ttbr0)return vti_fail(v,VTI_READBACK);
    if(!vti_r64(v,cb+0x28,&wide))return 0;
    if(wide!=v->image.ttbr1)return vti_fail(v,VTI_READBACK);
    if(!vti_wr(v,cb,v->image.sctlr) || !vti_rd(v,cb,&value))return 0;
    if((value&~0x1000u)!=(v->image.sctlr&~0x1000u))return vti_fail(v,VTI_READBACK);
    if(!vti_rd(v,cb+0x58,&value))return 0;
    if(value&0xc00001feu)return vti_fail(v,VTI_READBACK);
    if(!vti_wr(v,0x800+4*v->slot,0x800000b6u) || !vti_rd(v,0x800+4*v->slot,&value))return 0;
    if(value!=0x800000b6u)return vti_fail(v,VTI_READBACK);
    if(!vti_wr(v,0xc00+4*v->slot,v->bank) || !vti_rd(v,0xc00+4*v->slot,&value))return 0;
    if(value!=v->bank)return vti_fail(v,VTI_READBACK);
    v->attached=1;return 1;
}
#endif
