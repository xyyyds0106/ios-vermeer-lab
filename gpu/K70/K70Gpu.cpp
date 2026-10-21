/* SPDX-License-Identifier: MIT */
#include "K70Gpu.h"
#include "../NativeAdreno/NativeAdrenoPwrup.h"
#include "../NativeAdreno/NativeAdrenoA740HwInit.h"
#include "../AdrenoPM4/AdrenoPM4.h"
#include <string.h>

namespace {
constexpr uint64_t va[]={0x100000,0x200000,0x300000,0x304000,0x308000};
constexpr size_t sizes[]={0x20000,0x8000,0x1000,0x1000,0x1000};
bool window(const K70GpuWindow &w,uint64_t pa,size_t bytes)
{return w.cpu && !((uintptr_t)w.cpu&7) && w.physical==pa && w.bytes==bytes && bytes-1<=UINTPTR_MAX-(uintptr_t)w.cpu;}
bool fits(const K70GpuWindow &w,size_t at,size_t n)
{return !(at&(n-1)) && at<=w.bytes && n<=w.bytes-at;}
}
int K70Gpu::guard()
{
    if(failed_ || !held_)return 1;
    uint64_t t;
    if(c_.platform.now_us(c_.platform.cookie,&t) || t<last_)return failed_=true;
    last_=t;r_->elapsed_us=t-begin_;
    if(t-begin_>=c_.total_timeout_us || c_.platform.lease(c_.platform.cookie))return failed_=true;
    if(c_.platform.now_us(c_.platform.cookie,&t) || t<last_)return failed_=true;
    last_=t;r_->elapsed_us=t-begin_;
    if(t-begin_>=c_.total_timeout_us)return failed_=true;
    return 0;
}
IOReturn K70Gpu::fail(IOReturn s)
{failed_=true;r_->status=s;r_->hardware_complete=0;return s;}
int K70Gpu::gpuRead(void *p,uint32_t reg,uint32_t *v)
{auto &s=*static_cast<K70Gpu *>(p);size_t at=(size_t)reg*4;
 if(!v || !fits(s.c_.gpu,at,4) || s.guard())return 1;
 *v=*reinterpret_cast<volatile uint32_t *>(s.c_.gpu.cpu+at);return s.guard();}
int K70Gpu::gpuWrite(void *p,uint32_t reg,uint32_t v)
{auto &s=*static_cast<K70Gpu *>(p);size_t at=(size_t)reg*4;
 if(!fits(s.c_.gpu,at,4) || s.guard())return 1;
 *reinterpret_cast<volatile uint32_t *>(s.c_.gpu.cpu+at)=v;return s.guard();}
int K70Gpu::barrier(void *p)
{auto &s=*static_cast<K70Gpu *>(p);if(s.guard())return 1;
#if defined(__aarch64__)
 __asm__ volatile("dsb sy":::"memory");
#else
 __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
 return s.guard();}
int K70Gpu::smmuRead32(void *p,size_t at,uint32_t *v)
{auto &s=*static_cast<K70Gpu *>(p);if(!v || !fits(s.c_.smmu,at,4) || s.guard())return 1;
 *v=*reinterpret_cast<volatile uint32_t *>(s.c_.smmu.cpu+at);return s.guard();}
int K70Gpu::smmuRead64(void *p,size_t at,uint64_t *v)
{auto &s=*static_cast<K70Gpu *>(p);if(!v || !fits(s.c_.smmu,at,8) || s.guard())return 1;
 *v=*reinterpret_cast<volatile uint64_t *>(s.c_.smmu.cpu+at);return s.guard();}
int K70Gpu::smmuWrite32(void *p,size_t at,uint32_t v)
{auto &s=*static_cast<K70Gpu *>(p);if(!fits(s.c_.smmu,at,4) || s.guard())return 1;
 *reinterpret_cast<volatile uint32_t *>(s.c_.smmu.cpu+at)=v;return s.guard();}
int K70Gpu::smmuWrite64(void *p,size_t at,uint64_t v)
{auto &s=*static_cast<K70Gpu *>(p);if(!fits(s.c_.smmu,at,8) || s.guard())return 1;
 *reinterpret_cast<volatile uint64_t *>(s.c_.smmu.cpu+at)=v;return s.guard();}
int K70Gpu::publish(void *p,const volatile uint32_t *words,size_t bytes)
{auto &s=*static_cast<K70Gpu *>(p);if(s.guard())return 1;
 for(unsigned i=1;i<=2;++i)if(words==s.bo_[i].mapping() && bytes==s.bo_[i].bytes())
    return s.bo_[i].publishForDevice(0,bytes,i==2)!=kIOReturnSuccess || s.guard();
 return 1;}
int K70Gpu::acquire(void *p,volatile uint32_t *words,size_t bytes)
{auto &s=*static_cast<K70Gpu *>(p);if(s.guard() || words!=s.bo_[2].mapping() || bytes!=sizes[2])return 1;
 if(s.context_.observe())return s.failed_=true;
 NativeAdrenoIrqResult fault{};
 if(s.irq_.handle(&fault))return 1;
 s.r_->fault_flags|=fault.flags;
 if(fault.flags&NativeAdrenoIrqFatal)return s.failed_=true;
 return s.bo_[2].acquireDeviceWrites(0,bytes)!=kIOReturnSuccess || s.guard();}
uint64_t K70Gpu::now(void *p)
{auto &s=*static_cast<K70Gpu *>(p);return s.guard()?UINT64_MAX:s.last_;}
int K70Gpu::idle(void *p)
{auto &s=*static_cast<K70Gpu *>(p);return s.guard() || s.c_.platform.idle(s.c_.platform.cookie) || s.guard();}
int K70Gpu::hwInit()
{
    auto wr=[&](uint32_t reg,uint32_t value){return gpuWrite(this,reg,value);};
    auto wr64=[&](uint32_t reg,uint64_t value){return wr(reg,(uint32_t)value)||wr(reg+1,(uint32_t)(value>>32));};
    auto gmu=[&](uint32_t reg,uint32_t value){
        /* Public register indices are GPU-relative dwords; K70's named GMU
         * origin is 0x68000, never sheng's 0x6a000. Subtract only after bound. */
        size_t absolute=(size_t)reg*4;
        if(absolute<0x68000 || !fits(c_.gmu,absolute-0x68000,4) || guard())return 1;
        *reinterpret_cast<volatile uint32_t *>(c_.gmu.cpu+absolute-0x68000)=value;return guard();};
    uint32_t clock=0,eco=0;
    if(wr(A740_GBIF_HALT,0)||gpuRead(this,A740_GBIF_HALT,&clock)||wr(A740_RBBM_GBIF_HALT,0)||
       gpuRead(this,A740_RBBM_GBIF_HALT,&clock)||wr(A740_RBBM_SECVID_TSB_CNTL,0)||
       wr64(A740_RBBM_SECVID_TSB_TRUSTED_BASE,0)||wr(A740_RBBM_SECVID_TSB_TRUSTED_SIZE,0)||
       gmu(A740_GPU_GMU_AO_GMU_CGC_MODE_CNTL,0x20202)||gmu(A740_GPU_GMU_AO_GMU_CGC_DELAY_CNTL,0x10111)||
       gmu(A740_GPU_GMU_AO_GMU_CGC_HYST_CNTL,0x5555)||gpuRead(this,A740_RBBM_CLOCK_CNTL,&clock))return 1;
    if(clock!=0x8aa8aa82){
        for(unsigned i=0;i<NATIVE_ADRENO_A740_HWCG_COUNT;++i)
            if(wr(NativeAdrenoA740Hwcg[i][0],NativeAdrenoA740Hwcg[i][1]))return 1;
        if(wr(A740_RBBM_CLOCK_CNTL,0x8aa8aa82))return 1;
    }
    if(wr(A740_RBBM_GBIF_CLIENT_QOS_CNTL,0x2120212)||wr(A740_UCHE_GBIF_GX_CONFIG,0x10240e0)||
       wr(A740_RBBM_PERFCTR_GPU_BUSY_MASKED,0xffffffff)||wr64(A740_UCHE_TRAP_BASE,0x1fffffffff000ull)||
       wr64(A740_UCHE_WRITE_THRU_BASE,0x1fffffffff000ull)||wr64(A740_UCHE_GMEM_RANGE_MIN,0x1000000)||
       wr64(A740_UCHE_GMEM_RANGE_MAX,0x12fffff)||wr(A740_UCHE_CACHE_WAYS,1u<<23)||wr(A740_CP_AHB_CNTL,1)||
       wr(A740_RBBM_PERFCTR_CNTL,1)||wr(A740_RBBM_INTERFACE_HANG_INT_CNTL,(1u<<30)|0xcfffff)||
       wr(A740_UCHE_CLIENT_PF,0x81)||wr(A740_CP_PROTECT_CNTL,0xb))return 1;
    for(unsigned i=0;i<NATIVE_ADRENO_A740_PROTECT_COUNT;++i)
        if(wr(A740_CP_PROTECT+NativeAdrenoA740Protect[i][0],NativeAdrenoA740Protect[i][1]))return 1;
    if(wr(A740_UCHE_CMDQ_CONFIG,0x6690e)||wr(A740_CP_APRIV_CNTL,0x6f)||wr(A740_CP_BV_APRIV_CNTL,0xf)||
       wr(A740_CP_LPAC_APRIV_CNTL,0xf)||gpuRead(this,A740_RB_CMP_DBG_ECO_CNTL,&eco)||
       wr(A740_RB_CMP_DBG_ECO_CNTL,eco|(1u<<11))||wr(A740_RB_NC_MODE_CNTL,0x816)||
       wr(A740_TPL1_NC_MODE_CNTL,6)||wr(A740_SP_NC_MODE_CNTL,0x26))return 1;
    for(unsigned pipe=1;pipe<=2;++pipe)
        if(wr(A740_CP_APERTURE_CNTL_HOST,pipe<<12)||wr(A740_GRAS_NC_MODE_CNTL,3u<<5))return 1;
    return wr(A740_CP_APERTURE_CNTL_HOST,0)||wr(A740_UCHE_MODE_CNTL,3u<<21)||wr(A740_RBBM_NC_MODE_CNTL,1)||
        wr(A740_RBBM_INT_0_MASK,0)||wr64(A740_CP_SQE_INSTR_BASE,va[0])||barrier(this);
}
IOReturn K70Gpu::run(const K70GpuConfig *c,K70GpuResult *r)
{
    if(!r)return kIOReturnBadArgument;
    *r={};r->resources_must_retain=1;
    if(used_)return r->status=kIOReturnBusy;
    if(!c || !c->platform.reserve || !c->platform.lease || !c->platform.now_us ||
       !c->platform.idle || !c->platform.boot_gmu || c->a740_profile_verified!=1 ||
       !window(c->gpu,0x3d00000,0x40000)||!window(c->gmu,0x3d68000,0x37000)||
       !window(c->smmu,0x3da0000,0x40000)||c->bank>63||!c->asid||c->asid>65535||
       c->slots[0]>127||c->slots[1]>127||c->slots[0]==c->slots[1]||c->sids[0]!=0||c->sids[1]!=1||
       !c->total_timeout_us||c->total_timeout_us>10000000)
        return r->status=kIOReturnBadArgument;
    c_=*c;r_=r;used_=true;r->stage=K70GpuFirmware;
    NativeAdrenoFirmwareView fw{};
    if(!c->sqe || c->sqe_bytes<8 || (c->sqe_bytes&3) || c->sqe_bytes-4>sizes[0] ||
       NativeAdrenoFirmware::validate(c->sqe,c->sqe_bytes,&c->sqe_rules,&fw))return fail(kIOReturnBadArgument);
    r->stage=K70GpuPlatform;
    if(c_.platform.now_us(c_.platform.cookie,&begin_))return fail(kIOReturnIOError);
    last_=begin_;
    IOReturn status=c_.platform.reserve(c_.platform.cookie);held_=true;
    if(status || guard())return fail(status?status:kIOReturnNotReady);
    r->stage=K70GpuGmu;
    status=c_.platform.boot_gmu(c_.platform.cookie);
    if(status || guard())return fail(status?status:kIOReturnNotReady);
    r->stage=K70GpuAllocation;
    if((status=tables_.allocate(64*4096,40,true,64*4096)))return fail(status);
    for(unsigned i=0;i<5;++i){
        if(guard())return fail(kIOReturnNotReady);
        if((status=bo_[i].allocate(sizes[i],40,true,sizes[i])))return fail(status);
        r->logical_bytes[i]=bo_[i].bytes();r->cpu_owned_bytes[i]=bo_[i].ownedBytes();r->gpu_spans[i]=bo_[i].spanCount();
    }
    r->stage=K70GpuContext;
    smmu_io_={this,c_.smmu.bytes,smmuRead32,smmuRead64,smmuWrite32,smmuWrite64,barrier};
    if(guard())return fail(kIOReturnNotReady);
    if((status=context_.prepare(&tables_,0,0,0x1000000,4096,c_.bank,c_.asid,1000,40))||
       (status=context_.program(&smmu_io_)))return fail(status);
    const ArmSmmu4KStream routes[]={{c_.slots[0],c_.sids[0]},{c_.slots[1],c_.sids[1]}};
    if((status=context_.attach(routes,2)))return fail(status);
    NativeAdrenoVmRegion lower{&tables_,context_.arenaHandle(),context_.stateHandle()};
    if((status=vm_.attach(&smmu_io_,context_.configHandle(),context_.streamsHandle(),&lower,nullptr,128,1000)))return fail(status);
    for(unsigned i=0;i<5;++i){
        if(guard())return fail(kIOReturnNotReady);
        /* Cached CPU and GPU views match; explicit PoC operations stay on.
         * Global io_coherent defaults zero: no unproved coherence shortcut. */
        if((status=vm_.map(&bo_[i],0,va[i],i?3u:5u,1,3,&handles_[i])))return fail(status);
    }
    memcpy(bo_[0].mapping(),fw.bytes+4,fw.length-4);
    if((status=bo_[0].publishForDevice(0,bo_[0].bytes(),false)) || guard())return fail(status?status:kIOReturnNotReady);
    ring_io_={this,gpuWrite,gpuRead,barrier};
    r->stage=K70GpuHardware;
    if(hwInit())return fail(kIOReturnIOError);
    if((status=irq_.attach(&ring_io_)))return fail(status);
    NativeAdrenoRingCache cache{this,publish,acquire};
    if((status=ring_.attach(&ring_io_,&cache,bo_[1].mapping(),sizes[1],va[1],bo_[2].mapping(),sizes[2],va[2])))return fail(status);
    /* Publish the CPU-zeroed timestamp page BEFORE SQE may run or overwrite it. */
    if(publish(this,static_cast<volatile uint32_t *>(bo_[2].mapping()),sizes[2])||
       gpuWrite(this,A740_CP_BV_RB_RPTR_ADDR,(uint32_t)(va[2]+8))||
       gpuWrite(this,A740_CP_BV_RB_RPTR_ADDR+1,0)||gpuWrite(this,A740_CP_SQE_CNTL,1))return fail(kIOReturnIOError);
    size_t written=0;
    if((status=NativeAdrenoPwrupList::buildA740(gpuRead,this,static_cast<uint8_t *>(bo_[3].mapping()),sizes[3],&written))||
       (status=bo_[3].publishForDevice(0,sizes[3],false))||guard())return fail(status?status:kIOReturnNotReady);
    r->stage=K70GpuCp;
    NativeAdrenoCpInitConfig init{va[0],va[3],1};
    NativeAdrenoRingClock clock{this,now,idle};
    if((status=cp_.run(&ring_io_,&ring_,&init,&r->cp_fence)))return fail(status);
    if(ring_.wait(r->cp_fence,&clock,200000,&r->observed_fence)!=NativeAdrenoRingFenceComplete||guard())return fail(kIOReturnTimeout);
    /* a6xx_gpu.c authenticates ZAP and exits secure mode after ME_INIT,
     * before ordinary memory work. A CP-init fence alone proves no such
     * transition and must not authorize the scratch write. */
    r->cp_ready=1;return r->status=kIOReturnSuccess;
}
IOReturn K70Gpu::verifyScratch()
{
    if(!r_ || !r_->cp_ready || !r_->normal_mode_ready || guard())return kIOReturnNotReady;
    if(scratch_attempted_)return kIOReturnBusy;
    scratch_attempted_=true;auto *r=r_;IOReturn status;
    NativeAdrenoRingClock clock{this,now,idle};
    r->stage=K70GpuScratch;
    auto *scratch=static_cast<volatile uint32_t *>(bo_[4].mapping());
    for(size_t i=0;i<sizes[4]/4;++i)scratch[i]=0xdeadbeef;
    if((status=bo_[4].publishForDevice(0,sizes[4],true))||guard())return fail(status?status:kIOReturnNotReady);
    /* The CPU writes only the sentinel. The three different expected words
     * exist in ring memory; only the GPU writes them into scratch. */
    uint32_t words[6];
    if(AdrenoPM4Type7(0x3d,5,&words[0])!=AdrenoPM4Success)return fail(kIOReturnIOError);
    words[1]=(uint32_t)va[4];words[2]=0;words[3]=0x4b373047;words[4]=0xb4c8cfb8;words[5]=0x740c0de;
    if((status=ring_.submit(words,6,&r->write_fence)))return fail(status);
    if(ring_.wait(r->write_fence,&clock,200000,&r->observed_fence)!=NativeAdrenoRingFenceComplete||guard())return fail(kIOReturnTimeout);
    if((status=bo_[4].acquireDeviceWrites(0,sizes[4]))||guard())return fail(status?status:kIOReturnNotReady);
    for(unsigned i=0;i<4;++i)r->scratch[i]=scratch[i];
    for(size_t i=0;i<sizes[4]/4;++i){
        uint32_t expected=i<3?words[i+3]:0xdeadbeef;
        if(scratch[i]!=expected)return fail(kIOReturnIOError);
        ++r->matched_words;
    }
    if(guard())return fail(kIOReturnNotReady);
    r->hardware_complete=1;r->stage=K70GpuComplete;return r->status=kIOReturnSuccess;
}
IOReturn K70Gpu::leaveSecureAfterPas(uint32_t *fence)
{
    if(!fence)return kIOReturnBadArgument;*fence=0;
    if(!r_ || !r_->cp_ready || guard())return kIOReturnNotReady;
    if(r_->normal_mode_ready)return kIOReturnBusy;
    uint32_t words[2]={0,0};
    if(AdrenoPM4Type7(0x66,1,&words[0])!=AdrenoPM4Success)return fail(kIOReturnIOError);
    IOReturn status=ring_.submit(words,2,fence);if(status)return fail(status);
    NativeAdrenoRingClock clock{this,now,idle};uint32_t observed=0;
    if(ring_.wait(*fence,&clock,200000,&observed)!=NativeAdrenoRingFenceComplete || guard())return fail(kIOReturnTimeout);
    r_->normal_mode_ready=1;
    return kIOReturnSuccess;
}
