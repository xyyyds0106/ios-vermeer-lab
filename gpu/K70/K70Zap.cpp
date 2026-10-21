/* SPDX-License-Identifier: MIT */
#include "K70Zap.h"
#include <IOKit/IOLib.h>
#include <mach/mach_types.h>
#include <string.h>
extern "C" vm_offset_t ml_io_map_unmappable(vm_offset_t,vm_size_t,unsigned int);
extern "C" void ml_io_unmap(vm_offset_t,vm_size_t);
namespace {
uint16_t u16(const uint8_t *p){return p[0]|uint16_t(p[1])<<8;}
uint32_t u32(const uint8_t *p){return u16(p)|uint32_t(u16(p+2))<<16;}
constexpr uint64_t reserved=0x9b09a000,capacity=0x2000;
}
int K70Zap::guard()
{uint64_t t;if(failed_||io_.now_us(io_.cookie,&t)||t<last_)return failed_=true;last_=t;
 if(t-began_>=3000000||io_.lease(io_.cookie))return failed_=true;
 if(io_.now_us(io_.cookie,&t)||t<last_)return failed_=true;last_=t;
 return t-began_>=3000000?(failed_=true):0;}
int K70Zap::smc(unsigned command,uint64_t arginfo,uint64_t a,uint64_t b,uint64_t c,unsigned index)
{
    const uint64_t fn=UINT64_C(0x42000200)|command;uint64_t first=fn,resume=0;
    for(unsigned attempts=0;attempts<4096;++attempts){
        if(guard())return 1;
#if defined(__aarch64__)
        register uint64_t x0 __asm__("x0")=first,x1 __asm__("x1")=arginfo,x2 __asm__("x2")=a,x3 __asm__("x3")=b;
        register uint64_t x4 __asm__("x4")=c,x5 __asm__("x5")=0,x6 __asm__("x6")=resume,x7 __asm__("x7")=0;
        __asm__ volatile("smc #0":"+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3),"+r"(x4),"+r"(x5),"+r"(x6),"+r"(x7)
            ::"x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","cc","memory");
#else
        /* Host cannot perform SCM: explicit refusal, never simulated success. */
        uint64_t x0=UINT64_MAX,x1=0,x6=0;(void)arginfo;(void)a;(void)b;(void)c;(void)first;(void)resume;
#endif
        r_->smc_status[index]=(int64_t)x0;r_->pas_status[index]=x1;
        if(guard())return 1;
        if(x0==1){first=1;resume=x6;++r_->resumes;continue;}
        if((int64_t)x0==-12 && r_->busy_retries<20){
            ++r_->busy_retries;first=fn;resume=0;
            if(io_.wait_us(io_.cookie,30000)||guard())return 1;continue;
        }
        return x0 || x1;
    }
    return 1;
}
IOReturn K70Zap::run(const uint8_t *image,size_t bytes,const NativeAdrenoFirmwareRules *rules,
    const K70ZapServices *services,K70ZapResult *r)
{
    if(!r || !services || !services->lease || !services->now_us || !services->wait_us)return kIOReturnBadArgument;
    if(r_ || r->used)return kIOReturnBusy;
    *r={};r->resources_must_retain=1;
    NativeAdrenoFirmwareView fw{};
    if(NativeAdrenoFirmware::validate(image,bytes,rules,&fw) || bytes<52 ||
       memcmp(image,"\177ELF",4)||image[4]!=1||image[5]!=1||image[6]!=1)return kIOReturnBadArgument;
    const uint32_t off=u32(image+28);const unsigned count=u16(image+44);
    if(u16(image+42)!=32 || count<2 || count>16 || off>bytes || count*32>bytes-off)return kIOReturnBadArgument;
    auto ph=[&](unsigned i,unsigned field){return u32(image+off+i*32+field*4);};
    if(ph(0,0)==1)return kIOReturnBadArgument;
    unsigned hash=0;uint64_t lo=UINT64_MAX,hi=0;bool relocate=false;
    for(unsigned i=1;i<count;++i)if(((ph(i,6)>>24)&7)==2){if(hash)return kIOReturnBadArgument;hash=i;}
    if(!hash)return kIOReturnBadArgument;
    for(unsigned i=0;i<count;++i){
        if(ph(i,0)!=1 || ((ph(i,6)>>24)&7)==2 || !ph(i,5))continue;
        if(ph(i,4)>ph(i,5)||ph(i,1)>bytes||ph(i,4)>bytes-ph(i,1))return kIOReturnBadArgument;
        uint64_t end=(uint64_t(ph(i,3))+ph(i,5)+4095)&~UINT64_C(4095);
        if(ph(i,3)<lo)lo=ph(i,3);if(end>hi)hi=end;
        relocate|=!!(ph(i,6)&(1u<<27));
    }
    if(lo==UINT64_MAX || hi<=lo || hi-lo>capacity)return kIOReturnBadArgument;
    uint64_t meta=ph(0,4),hash_at=ph(hash,1),hash_bytes=ph(hash,4);
    if(meta<off+count*32 || meta>bytes || hash_at>bytes || hash_bytes>bytes-hash_at ||
        meta>0x4000 || hash_bytes>0x4000-meta)return kIOReturnBadArgument;
    const uint64_t base=relocate?lo:reserved;
    for(unsigned i=0;i<count;++i){
        if(ph(i,0)!=1 || ((ph(i,6)>>24)&7)==2 || !ph(i,5))continue;
        if(ph(i,3)<base || uint64_t(ph(i,3))-base>capacity || ph(i,5)>capacity-(ph(i,3)-base))return kIOReturnBadArgument;
        for(unsigned j=0;j<i;++j){
            if(ph(j,0)!=1 || ((ph(j,6)>>24)&7)==2 || !ph(j,5))continue;
            if(uint64_t(ph(i,3))<uint64_t(ph(j,3))+ph(j,5) && uint64_t(ph(j,3))<uint64_t(ph(i,3))+ph(i,5))return kIOReturnBadArgument;
        }
    }
    r_=r;io_=*services;r->used=1;
    if(io_.now_us(io_.cookie,&began_))return kIOReturnIOError;last_=began_;
    if(guard())return kIOReturnNotReady;
    const size_t owned=(size_t)((meta+hash_bytes+4095)&~UINT64_C(4095));
    r->stage=1;
    IOReturn status=metadata_.allocate(owned,40,true,owned);if(status)return status;
    if(metadata_.spanCount()!=1)return kIOReturnUnsupported;
    uint64_t pa=metadata_.spans()[0].physical,n=metadata_.ownedBytes();
    if(pa<reserved+capacity && reserved<pa+n)return kIOReturnBadArgument;
    memcpy(metadata_.mapping(),image,(size_t)meta);
    memcpy(static_cast<uint8_t *>(metadata_.mapping())+meta,image+hash_at,(size_t)hash_bytes);
    if(metadata_.holdForDevice()||metadata_.publishForDevice(0,owned,true)||guard())return kIOReturnIOError;
    r->stage=2;if(smc(1,2|(2u<<6),13,pa,0,0))return kIOReturnIOError;
    r->stage=3;if(relocate && smc(2,3,13,reserved,hi-lo,1))return kIOReturnIOError;
    if(guard())return kIOReturnNotReady;
    /* Fresh unmanaged IO view: unlike an IOMemoryDescriptor cache-mode map,
     * this does NOT change cache attributes of the neighbouring 8KB which
     * share a 16KB physical CPU page with this exact reserved region. */
    vm_offset_t mapping=ml_io_map_unmappable((vm_offset_t)reserved,(vm_size_t)capacity,7 /* VM_WIMG_IO */);
    if(!mapping)return kIOReturnNoMemory;
    r->stage=4;
    auto *dest=reinterpret_cast<volatile uint8_t *>(mapping);
    for(unsigned i=0;i<count;++i){
        if(ph(i,0)!=1 || ((ph(i,6)>>24)&7)==2 || !ph(i,5))continue;
        if(guard()){ml_io_unmap(mapping,(vm_size_t)capacity);return kIOReturnNotReady;}
        size_t at=(size_t)(ph(i,3)-base),file=ph(i,4),mem=ph(i,5),from=ph(i,1);
        for(size_t b=0;b<file;++b)dest[at+b]=image[from+b];
        for(size_t b=file;b<mem;++b)dest[at+b]=0;
    }
#if defined(__aarch64__)
    __asm__ volatile("dsb sy":::"memory");
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
    ml_io_unmap(mapping,(vm_size_t)capacity);
    if(guard())return kIOReturnNotReady;
    r->stage=5;if(smc(5,1,13,0,0,2)||guard())return kIOReturnIOError;
    r->stage=6;r->authenticated=1;return kIOReturnSuccess;
}
