/* SPDX-License-Identifier: MIT */
#include "K70GpuNative.h"
#include "K70GmuRegs.h"
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <kern/clock.h>

namespace {
class Bootstrap final {
public:
    K70GpuNativeInput in{};K70GpuNativeResult result{};K70GpuPowerLease power{};
    K70GmuProfile profile{};K70GmuSetupWrite setup[64]{};AdrenoHfiStartupConfig startup{};
    K70Gpu gpu;K70GmuNative gmu;K70Zap zap;
    IOMemoryMap *maps[10]{};K70GpuWindow windows[10]{};
    uint64_t began=0,last=0;bool failed=false,powered=false;
    static constexpr uint64_t physical[10]={0x3d00000,0x3d68000,0x3da0000,0x3d50000,0xb280000,0x3d9e000,0x171000,0x152000,0x3d90000,0x17100000};
    static constexpr size_t bytes[10]={0x40000,0x37000,0x40000,0x10000,0x10000,0x1000,0x1000,0x1000,0xa000,0x10000};
    static int time(void *,uint64_t *out){if(!out)return 1;clock_sec_t s;clock_usec_t us;clock_get_system_microtime(&s,&us);*out=(uint64_t)s*1000000+us;return 0;}
    int guard(){uint64_t t;
        if(failed || !powered || time(this,&t) || t<last)return failed=true;
        last=t;
        if(t-began>=10000000 || K70GpuPowerValidate(&power))return failed=true;
        if(time(this,&t)||t<last)return failed=true;last=t;
        return t-began>=10000000?(failed=true):0;
    }
    static int lease(void *p){return static_cast<Bootstrap *>(p)->guard();}
    static int delay(void *p,unsigned us){auto &s=*static_cast<Bootstrap *>(p);if(s.guard()||us>100000)return 1;IODelay(us);return s.guard();}
    static int idle(void *p){return delay(p,10);}
    static int barrier(void *p){auto &s=*static_cast<Bootstrap *>(p);if(s.guard())return 1;
#if defined(__aarch64__)
        __asm__ volatile("dsb sy":::"memory");
#else
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
        return s.guard();}
    int read(unsigned w,size_t at,uint32_t *v){if(!v||w>=10||(at&3)||at>windows[w].bytes||windows[w].bytes-at<4||guard())return 1;
        *v=*reinterpret_cast<volatile uint32_t *>(windows[w].cpu+at);return guard();}
    int write(unsigned w,size_t at,uint32_t v){if(w>=9||(at&3)||at>windows[w].bytes||windows[w].bytes-at<4||guard())return 1;
        *reinterpret_cast<volatile uint32_t *>(windows[w].cpu+at)=v;return guard();}
    static int cr(void *p,unsigned w,size_t at,uint32_t *v){auto &s=*static_cast<Bootstrap *>(p);
        if(w==K70ClockGpucc)return s.read(8,at,v);
        if(w!=K70ClockGcc)return 1;
        if(at>=0x71000 && at<0x72000)return s.read(6,at-0x71000,v);
        return at==0x52000?s.read(7,0,v):1;}
    static int cw(void *p,unsigned w,size_t at,uint32_t v){auto &s=*static_cast<Bootstrap *>(p);
        if(w==K70ClockGpucc)return s.write(8,at,v);
        if(w!=K70ClockGcc)return 1;
        if(at>=0x71000 && at<0x72000)return s.write(6,at-0x71000,v);
        return at==0x52000?s.write(7,0,v):1;}
    int faultInterruptsMasked(){
        /* K70 DT SPI numbers; hardware GIC INTID = SPI+32. Do not alter
         * global enable, active, pending or routing. Refuse an IRQ another
         * owner enabled; keep all of these masked through the polling lease.
         * The generic kit's CFIE/GFIE may assert a masked pending source,
         * which is never delivered/unmasked as an unhandled CPU interrupt. */
        static const unsigned spi[]={300,304,305,673,677,678,679,680,681,682,683,684,685,686,687,
            422,476,574,575,576,577,659,661,664,665,666,668,669,699};
        for(unsigned irq:spi){unsigned id=irq+32;uint32_t enabled,active;
            if(read(9,0x100+4*(id/32),&enabled)||read(9,0x300+4*(id/32),&active)||
                ((enabled|active)&(1u<<(id%32))))return 1;
        }
        return 0;
    }
    int allocateSmmu(){
        uint32_t id0,id1,scr0,fault;
        if(read(2,0x20,&id0)||read(2,0x24,&id1)||read(2,0,&scr0)||read(2,0x48,&fault)||fault)return 1;
        const unsigned stride=(id1>>31)?0x10000:0x1000,pages=1u<<(((id1>>28)&7)+1);
        const unsigned banks=id1&255,s2=(id1>>16)&255,slots=id0&255;
        if(stride!=0x1000 || !banks || banks>64 || banks>pages || s2 || banks<2 || !slots || slots>128 || slots<3 ||
           pages>32 || 2u*pages*stride>windows[2].bytes || !(id0&(1u<<27)))return 1;
        /* Native kernel owns the WHOLE GPU SMMU lease. Never overwrite a
         * bootloader/foreign active bank or valid stream, even under that lease. */
        for(unsigned b=0;b<banks;++b){uint32_t v;if(read(2,(pages+b)*stride,&v)||v&1)return 1;}
        for(unsigned slot=0;slot<slots;++slot){uint32_t smr,s2cr;
            if(read(2,0x800+4*slot,&smr)||read(2,0xc00+4*slot,&s2cr))return 1;
            if((id0&(1u<<8))?(s2cr&(1u<<10)):(smr&(1u<<31)))return 1;
        }
        /* GPU bank0 is the Adreno SMMU platform ABI, not SID==bank. The
         * allocator checks it disabled and reserves a different GMU bank.
         * No live foreign context/stream exists; unique ASIDs1/2 are retained. */
        result.gpu_bank=0;result.gmu_bank=1;
        result.slots[0]=0;result.slots[1]=1;result.slots[2]=2;
        for(unsigned i=0;i<3;++i)if(write(2,0xc00+4*result.slots[i],2u<<16)||
            write(2,0x800+4*result.slots[i],0))return 1;
        /* MMU500 CPRE errata: clear only CPRE in the leased banks; do not
         * copy a sheng-observed ACTLR=0x301 or rewrite every context bank. */
        for(unsigned b=0;b<2;++b){size_t cb=(pages+b)*stride;uint32_t v;
            if(read(2,cb+4,&v)||write(2,cb+4,v&~2u))return 1;
        }
        uint32_t desired=(scr0|0x406)&~1u;
        if(id0&(1u<<8))desired|=8;else desired&=~8u;
        if(write(2,0,desired)||barrier(this)||read(2,0,&scr0)||(scr0&0x40f)!=(desired&0x40f))return 1;
        return 0;
    }
    static IOReturn reserve(void *p){auto &s=*static_cast<Bootstrap *>(p);s.result.stage=1;
        if(time(p,&s.began))return kIOReturnIOError;s.last=s.began;
        int status=K70GpuPowerAcquire(&s.in.power,&s.power);s.powered=true;
        if(status || s.guard())return kIOReturnNotReady;
        /* Mapping creates no read of a powered-off block. The power lease
         * establishes controller access; subsequent clock/GMU gates order I/O. */
        for(unsigned i=0;i<10;++i){
            IODeviceMemory *memory=IODeviceMemory::withRange(physical[i],bytes[i]);
            if(!memory)return kIOReturnNoMemory;
            s.maps[i]=memory->map(kIOMapAnywhere|kIOMapInhibitCache);memory->release();
            if(!s.maps[i] || s.maps[i]->getLength()<bytes[i])return kIOReturnNoMemory;
            s.windows[i]={reinterpret_cast<volatile uint8_t *>(s.maps[i]->getVirtualAddress()),physical[i],bytes[i]};
        }
        if(s.faultInterruptsMasked())return kIOReturnNotReady;
        s.result.stage=2;K70ClockIo io{p,lease,time,delay,cr,cw,barrier};
        if(K70GpuClockStart(&s.result.clock,&io))return kIOReturnNotReady;
        uint32_t reset=0,gx=0;
        if(s.read(1,(REG_GMU_CM3_SYSRESET*4)-0x68000,&reset)||reset!=1||
           s.read(1,(0x1f8d0*4)-0x68000,&gx)||(gx&3)!=3)return kIOReturnNotReady;
        s.result.stage=3;
        return s.allocateSmmu()?kIOReturnNotReady:kIOReturnSuccess;
    }
    static IOReturn boot(void *p){auto &s=*static_cast<Bootstrap *>(p);s.result.stage=4;
        K70GmuNativeConfig c{};c.cookie=p;c.lease=lease;c.now_us=time;c.wait_us=delay;
        const unsigned map[]={1,3,4,0,5};for(unsigned i=0;i<5;++i)c.windows[i]=s.windows[map[i]];
        c.smmu=s.windows[2];c.bank=s.result.gmu_bank;c.asid=2;c.slot=s.result.slots[2];
        c.chip_id=s.in.chip_id;c.profile=s.in.gmu_profile;c.firmware=s.in.gmu;c.firmware_bytes=s.in.gmu_bytes;
        c.startup=s.in.startup;c.perf_index=s.in.perf_index;c.bandwidth_index=s.in.bandwidth_index;c.total_timeout_us=5000000;
        return s.gmu.run(&c,&s.result.gmu);
    }
};
Bootstrap *retained;unsigned attempted;
}
extern "C" IOReturn K70GpuNativeStart(const K70GpuNativeInput *in,const K70GpuNativeResult **out)
{
    if(!out)return kIOReturnBadArgument;*out=nullptr;
    if(!in||!in->gmu_profile||in->gmu_profile->qualified!=1||in->chip_id!=in->gmu_profile->chip_id||
       !in->gmu_profile->setup||!in->gmu_profile->setup_count||in->gmu_profile->setup_count>64||
       !in->startup||AdrenoHfiStartupValidate(in->startup)!=AdrenoHfiStartupSuccess||
       in->power.gmu_hz!=220000000||in->power.gpu_hz!=220000000||!in->power.bandwidth_kbps)
        return kIOReturnBadArgument;
    NativeAdrenoFirmwareView fw{};NativeAdrenoFirmwareRules gmu_rules{};
    gmu_rules.bytes=in->gmu_profile->firmware_bytes;
    for(unsigned i=0;i<32;++i)gmu_rules.sha256[i]=in->gmu_profile->sha256[i];
    if(NativeAdrenoFirmware::validate(in->sqe,in->sqe_bytes,&in->sqe_rules,&fw)||
       NativeAdrenoFirmware::validate(in->gmu,in->gmu_bytes,&gmu_rules,&fw)||
       NativeAdrenoFirmware::validate(in->zap,in->zap_bytes,&in->zap_rules,&fw))return kIOReturnBadArgument;
    unsigned zero=0;if(!__atomic_compare_exchange_n(&attempted,&zero,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return kIOReturnBusy;
    retained=new Bootstrap;if(!retained)return kIOReturnNoMemory;
    auto &s=*retained;s.in=*in;s.profile=*in->gmu_profile;s.startup=*in->startup;
    for(size_t i=0;i<s.profile.setup_count;++i)s.setup[i]=in->gmu_profile->setup[i];
    s.profile.setup=s.setup;s.in.gmu_profile=&s.profile;s.in.startup=&s.startup;
    s.result.used=1;s.result.resources_must_retain=1;*out=&s.result;
    /* Core prevalidates exact window shapes before reserve. Only reserve
     * obtains their mappings, so execute reserve here and give the core a
     * real retained-lease validation callback instead of running it twice. */
    IOReturn status=Bootstrap::reserve(&s);
    if(status){s.failed=true;s.result.status=status;K70GpuPowerQuarantine(&s.power);return status;}
    K70GpuConfig c{};c.platform={&s,[](void *p)->IOReturn{return Bootstrap::lease(p)?kIOReturnNotReady:kIOReturnSuccess;},Bootstrap::lease,Bootstrap::time,Bootstrap::idle,Bootstrap::boot};
    c.gpu=s.windows[0];c.gmu=s.windows[1];c.smmu=s.windows[2];
    c.bank=s.result.gpu_bank;c.asid=1;c.slots[0]=s.result.slots[0];c.slots[1]=s.result.slots[1];c.sids[1]=1;
    c.a740_profile_verified=1;c.sqe=s.in.sqe;c.sqe_bytes=s.in.sqe_bytes;c.sqe_rules=s.in.sqe_rules;c.total_timeout_us=10000000;
    status=s.gpu.run(&c,&s.result.gpu);s.result.stage=status?5:6;
    if(!status){
        K70ZapServices services{&s,Bootstrap::lease,Bootstrap::time,Bootstrap::delay};
        status=s.zap.run(s.in.zap,s.in.zap_bytes,&s.in.zap_rules,&services,&s.result.zap);
        if(!status){s.result.stage=7;status=s.gpu.leaveSecureAfterPas(&s.result.secure_fence);}
        if(!status)s.result.stage=8;
    }
    s.result.status=status;
    if(status){s.failed=true;K70GpuPowerQuarantine(&s.power);}return status;
}
