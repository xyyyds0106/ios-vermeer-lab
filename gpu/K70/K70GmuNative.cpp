/* SPDX-License-Identifier: MIT */
#include "K70GmuNative.h"
#include "K70GmuRegs.h"
#include "../NativeAdreno/NativeAdrenoSha256.h"
namespace {
const uint64_t pa[]={0x3d68000,0x3d50000,0xb280000,0x3d00000,0x3d9e000};
const size_t extent[]={0x37000,0x10000,0x10000,0x40000,0x1000};
bool valid(const K70GpuWindow &w,uint64_t physical,size_t bytes)
{return w.cpu && !((uintptr_t)w.cpu&7) && w.physical==physical && w.bytes==bytes && bytes-1<=UINTPTR_MAX-(uintptr_t)w.cpu;}
bool fits(const K70GpuWindow &w,size_t at,size_t bytes)
{return !(at&(bytes-1)) && at<=w.bytes && bytes<=w.bytes-at;}
}
int K70GmuNative::guard()
{
    uint64_t t;
    if(failed_ || c_.now_us(c_.cookie,&t) || t<last_)return failed_=true;
    last_=t;r_->elapsed_us=t-start_;
    if(t-start_>=c_.total_timeout_us || c_.lease(c_.cookie))return failed_=true;
    if(c_.now_us(c_.cookie,&t) || t<last_)return failed_=true;
    last_=t;r_->elapsed_us=t-start_;
    return t-start_>=c_.total_timeout_us?(failed_=true):0;
}
IOReturn K70GmuNative::fail(IOReturn s)
{failed_=true;d_.poisoned=1;d_.hfi.poisoned=1;r_->ready=0;return r_->status=s;}
int K70GmuNative::read(void *p,unsigned w,size_t at,uint32_t *v)
{auto &s=*static_cast<K70GmuNative *>(p);if(!v || w>=5 || !fits(s.c_.windows[w],at,4)||s.guard())return 1;
 *v=*reinterpret_cast<volatile uint32_t *>(s.c_.windows[w].cpu+at);return s.guard();}
int K70GmuNative::write(void *p,unsigned w,size_t at,uint32_t v)
{auto &s=*static_cast<K70GmuNative *>(p);if(w>=5 || !fits(s.c_.windows[w],at,4)||s.guard())return 1;
 *reinterpret_cast<volatile uint32_t *>(s.c_.windows[w].cpu+at)=v;return s.guard();}
int K70GmuNative::barrier(void *p)
{auto &s=*static_cast<K70GmuNative *>(p);if(s.guard())return 1;
#if defined(__aarch64__)
 __asm__ volatile("dsb sy":::"memory");
#else
 __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
 return s.guard();}
int K70GmuNative::clock(void *p,uint64_t *v)
{auto &s=*static_cast<K70GmuNative *>(p);if(!v || s.guard())return 1;*v=s.last_;return 0;}
int K70GmuNative::wait(void *p,unsigned us)
{auto &s=*static_cast<K70GmuNative *>(p);return s.guard() || s.c_.wait_us(s.c_.cookie,us)||s.guard();}
int K70GmuNative::hash(void *p,const void *v,size_t n,uint8_t *out)
{auto &s=*static_cast<K70GmuNative *>(p);if(s.guard())return 1;
 NativeAdrenoSha256::hash(static_cast<const uint8_t *>(v),n,out);return s.guard();}
int K70GmuNative::sr32(void *p,size_t at,uint32_t *v)
{auto &s=*static_cast<K70GmuNative *>(p);if(!v || !fits(s.c_.smmu,at,4)||s.guard())return 1;
 *v=*reinterpret_cast<volatile uint32_t *>(s.c_.smmu.cpu+at);return s.guard();}
int K70GmuNative::sr64(void *p,size_t at,uint64_t *v)
{auto &s=*static_cast<K70GmuNative *>(p);if(!v || !fits(s.c_.smmu,at,8)||s.guard())return 1;
 *v=*reinterpret_cast<volatile uint64_t *>(s.c_.smmu.cpu+at);return s.guard();}
int K70GmuNative::sw32(void *p,size_t at,uint32_t v)
{auto &s=*static_cast<K70GmuNative *>(p);if(!fits(s.c_.smmu,at,4)||s.guard())return 1;
 *reinterpret_cast<volatile uint32_t *>(s.c_.smmu.cpu+at)=v;return s.guard();}
int K70GmuNative::sw64(void *p,size_t at,uint64_t v)
{auto &s=*static_cast<K70GmuNative *>(p);if(!fits(s.c_.smmu,at,8)||s.guard())return 1;
 *reinterpret_cast<volatile uint64_t *>(s.c_.smmu.cpu+at)=v;return s.guard();}
int K70GmuNative::coreRead(uint32_t reg,uint32_t *v)
{size_t at=(size_t)reg*4;return at<0x68000?1:read(this,0,at-0x68000,v);}
int K70GmuNative::coreWrite(uint32_t reg,uint32_t v)
{size_t at=(size_t)reg*4;return at<0x68000?1:write(this,0,at-0x68000,v);}
int K70GmuNative::health()
{uint32_t ao,cm3,reset,ready;
 return guard()||context_.observe()||coreRead(REG_GMU_AO_HOST_INTERRUPT_STATUS,&ao)||
    coreRead(REG_GMU_GMU2HOST_INTR_INFO,&cm3)||coreRead(REG_GMU_CM3_SYSRESET,&reset)||
    coreRead(REG_GMU_HFI_CTRL_STATUS,&ready)||reset||!(ready&1)||
    (ao&K70_GMU_AO_FATAL_MASK)||(cm3&K70_GMU_CM3_FATAL_MASK)||guard();}
IOReturn K70GmuNative::run(const K70GmuNativeConfig *c,K70GmuNativeResult *r)
{
    if(!r)return kIOReturnBadArgument;
    *r={};r->resources_must_retain=1;
    if(used_)return r->status=kIOReturnBusy;
    if(!c || !c->lease || !c->now_us || !c->wait_us || !c->profile || c->profile->qualified!=1 ||
       c->chip_id!=c->profile->chip_id || !c->firmware || !c->startup ||
       c->bank>63 || !c->asid || c->asid>65535 || c->slot>127 ||
       !valid(c->smmu,0x3da0000,0x40000) || !c->perf_index ||
       c->perf_index>=c->startup->gpu_count || c->bandwidth_index>=c->startup->bandwidth_count ||
       !c->total_timeout_us || c->total_timeout_us>10000000)
        return r->status=kIOReturnBadArgument;
    for(unsigned i=0;i<5;++i)if(!valid(c->windows[i],pa[i],extent[i]))return r->status=kIOReturnBadArgument;
    NativeAdrenoFirmwareRules rules{};rules.bytes=c->profile->firmware_bytes;
    for(unsigned i=0;i<32;++i)rules.sha256[i]=c->profile->sha256[i];
    NativeAdrenoFirmwareView fw{};
    if(NativeAdrenoFirmware::validate(c->firmware,c->firmware_bytes,&rules,&fw) ||
       AdrenoHfiStartupValidate(c->startup)!=AdrenoHfiStartupSuccess)return r->status=kIOReturnBadArgument;
    c_=*c;r_=r;used_=true;
    if(c_.now_us(c_.cookie,&start_))return fail(kIOReturnIOError);
    last_=start_;if(guard())return fail(kIOReturnNotReady);
    uint32_t reset=0;
    /* Do not allocate/program a context while a previously running GMU can
     * still DMA. Native platform must hold its exclusively owned CM3 reset. */
    if(coreRead(REG_GMU_CM3_SYSRESET,&reset) || reset!=1)return fail(kIOReturnNotReady);
    r->stage=1;
    ArmSmmu4KIo io{this,c_.smmu.bytes,sr32,sr64,sw32,sw64,barrier};
    IOReturn status=context_.bringUp(&io,c_.bank,c_.asid,c_.slot,5,nullptr,&r->context);
    if(status || guard())return fail(status?status:kIOReturnNotReady);
    auto &a=context_.allocator();const auto &layout=a.layout();
    NativeAdrenoMemory *memory[]={&a.icache(),&a.dummy(),&a.debug(),&a.log(),&a.hfi()};
    uint64_t va[]={layout.icache_iova,layout.dummy_iova,layout.debug_iova,layout.log_iova,layout.hfi_iova};
    K70GmuConfig cfg{};cfg.chip_id=c_.chip_id;cfg.profile=c_.profile;
    cfg.firmware=fw.bytes;cfg.firmware_bytes=fw.length;cfg.poll_budget=200;
    K70GmuMemory *out[]={&cfg.icache,&cfg.dummy,&cfg.debug,&cfg.log,&cfg.hfi};
    for(unsigned i=0;i<5;++i){
        if(!memory[i]->writeCombined())return fail(kIOReturnNotReady);
        *out[i]={static_cast<volatile uint32_t *>(memory[i]->mapping()),memory[i]->bytes(),va[i]};
    }
    d_.io.cookie=this;d_.io.read32=read;d_.io.write32=write;d_.io.barrier=barrier;
    for(unsigned i=0;i<5;++i)d_.io.bytes[i]=extent[i];
    d_.services={this,hash,clock,wait};
    r->stage=2;
    /* a6xx_gmu_resume: AO interrupt clear/mask, with K70 origin. */
    if(coreWrite(0x23b04,UINT32_MAX)||coreWrite(0x23b06,~K70_GMU_AO_FATAL_MASK)||barrier(this))return fail(kIOReturnIOError);
    const K70GmuStartupLimits limits{1000,100,50000,2000000};
    if(K70GmuColdStart(&d_,&cfg,c_.startup,&limits,&r->startup)!=K70GmuSuccess||health())return fail(kIOReturnIOError);
    r->stage=3;
    /* Modern HFI GX_BW_PERF_VOTE, blocking ACK. Exact indices are from the
     * board's actual OPP/ARC/BCM configuration, never the sheng OPP table. */
    uint32_t payload[]={1,c_.perf_index,c_.bandwidth_index},header=0,reply[16];size_t n=0;
    if(health()||AdrenoHfiSend(&d_.hfi,30,payload,3,&header)!=AdrenoHfiSuccess||health())return fail(kIOReturnIOError);
    uint64_t command=last_;bool ack=false;
    for(unsigned i=0;i<1000;++i){
        if(health()||last_-command>=50000)return fail(kIOReturnTimeout);
        AdrenoHfiStatus hs=AdrenoHfiPoll(&d_.hfi,reply,16,&n,&r->perf_firmware_error);
        if(health()||last_-command>=50000)return fail(kIOReturnTimeout);
        if(hs==AdrenoHfiSuccess){ack=true;break;}
        if(hs!=AdrenoHfiEmpty||wait(this,100))return fail(kIOReturnIOError);
    }
    if(!ack)return fail(kIOReturnTimeout);
    r->stage=4;
    /* Fresh OOB: clear old ACK, verify clear, request GPU_SET bit30, observe
     * actual GMU ACK31, then verify A7XX GX power/clock OFF bits both clear. */
    uint32_t v=0;
    if(coreWrite(0x1f991,1u<<31)||barrier(this)||coreRead(0x1f992,&v)||(v&(1u<<31))||
       coreWrite(0x1f993,~(1u<<23))||coreWrite(0x1f994,1u<<30)||barrier(this))return fail(kIOReturnIOError);
    command=last_;ack=false;
    for(unsigned i=0;i<100;++i){
        if(health()||last_-command>=10000||coreRead(0x1f992,&v))return fail(kIOReturnTimeout);
        ++r->oob_polls;
        if(v&(1u<<31)){ack=true;break;}
        if(wait(this,100))return fail(kIOReturnIOError);
    }
    if(!ack||coreWrite(0x1f991,1u<<31)||barrier(this)||coreRead(0x1f8d0,&r->gx_status)||
       (r->gx_status&3)||health())return fail(kIOReturnNotReady);
    r->stage=5;r->ready=1;return r->status=kIOReturnSuccess;
}
