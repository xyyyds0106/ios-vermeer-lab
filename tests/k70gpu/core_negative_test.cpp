/* Production native core with invalid/missing prerequisites. No GPU peer. */
#include "K70Gpu.h"
#include "NativeAdrenoSha256.h"
#include <cassert>
#include <cstdio>
static unsigned reserve_calls,gmu_calls;
static uint64_t ticks;
static bool revoked;
static bool gmu_ready;
static unsigned cp_mode;
static uint8_t *gpu_registers;
static uint8_t *smmu_registers;
static IOReturn reserve(void *){++reserve_calls;return kIOReturnSuccess;}
static int lease(void *){
    /* Software W1C peripheral semantics; actual native code writes FSR=~0
     * and hardware clears it. A plain host byte array needs this peer step. */
    if(gmu_ready && smmu_registers)for(unsigned bank=0;bank<16;++bank){
        auto *fsr=reinterpret_cast<uint32_t *>(smmu_registers+0x20000+bank*4096+0x58);
        if(*fsr==UINT32_MAX)*fsr=0;
    }
    return revoked;
}
static int now(void *,uint64_t *v){*v=ticks;return 0;}
/* Independent software CP peer, used ONLY by this host executable. It reads
 * the real emitted ring stream and writes the peer's device memory/fence.
 * Production K70Gpu contains no emulation or success-pattern CPU store. */
static int idle(void *){
    ++ticks;
    if(!cp_mode || fixture.live.size()!=6)return 0;
    auto *regs=reinterpret_cast<uint32_t *>(gpu_registers);
    auto *ring=static_cast<uint32_t *>(fixture.live[2]->raw);
    auto *fence=static_cast<uint32_t *>(fixture.live[3]->raw);
    auto *scratch=static_cast<uint32_t *>(fixture.live[5]->raw);
    size_t at=regs[0x806],end=regs[0x807];
    while(at!=end){
        uint32_t h=ring[at++],type=h>>28;
        unsigned n=type==4?(h&127):(h&0x3fff),op=(h>>16)&127;
        assert((type==4 || type==7) && n<=end-at);
        if(type==7 && op==0x3d){
            assert(n==5 && ring[at]==0x308000 && ring[at+1]==0);
            if(cp_mode!=2)for(unsigned i=0;i<3;++i)scratch[i]=ring[at+2+i];
            if(cp_mode==5)scratch[1023]=0;
        }else if(type==7 && op==0x46){
            assert(n==4 && ring[at]==0x88000031 && ring[at+1]==0x300004 && !ring[at+2]);
            if(cp_mode!=3 || ring[at+3]==1)fence[1]=cp_mode==4?99:ring[at+3];
        }else if(type==7)assert((op==0x17&&n==1)||(op==0x48&&n==7));
        at+=n;
    }
    regs[0x806]=(uint32_t)end;
    if(cp_mode==6)revoked=true;
    if(cp_mode==7)*reinterpret_cast<uint32_t *>(smmu_registers+0x20000+0x58)=2;
    return 0;
}
static IOReturn no_gmu(void *){++gmu_calls;return gmu_ready?kIOReturnSuccess:kIOReturnNotReady;}
IOReturn NativeAdrenoQueryCpuNative(NativeAdrenoCpuState *s){*s={};s->kernel_page_bytes=16384;s->kernel_map_page_bytes=16384;return 0;}
IOReturn NativeAdrenoQueryCpu4K(NativeAdrenoCpuState *s){return NativeAdrenoQueryCpuNative(s);}
IOReturn NativeAdrenoMaintainOwnedPages(void *,size_t,NativeAdrenoCacheOperation op){return cp_mode==8 && op==NativeAdrenoInvalidate?kIOReturnIOError:0;}
IOReturn NativeAdrenoMaintainPhysicalPages(uint64_t,size_t,NativeAdrenoCacheOperation){return 0;}
IOReturn NativeAdrenoSetPagesCacheMode(const NativeAdrenoPhysicalSpan *,size_t,bool){return 0;}
IOReturn NativeAdrenoReassertWriteCombined(const NativeAdrenoPhysicalSpan *,size_t,uint32_t *n){*n=0;return 0;}
int main(){
    alignas(16) static uint8_t gpu[0x40000],gmu[0x37000],smmu[0x40000];
    uint8_t firmware[16]={1,2,3,4,5,6,7,8};
    K70GpuConfig c{};c.platform={nullptr,reserve,lease,now,idle,no_gmu};
    c.gpu={gpu,0x3d00000,sizeof gpu};c.gmu={gmu,0x3d68000,sizeof gmu};c.smmu={smmu,0x3da0000,sizeof smmu};
    gpu_registers=gpu;
    smmu_registers=smmu;
    c.asid=1;c.slots[1]=1;c.sids[1]=1;c.a740_profile_verified=1;
    c.sqe=firmware;c.sqe_bytes=sizeof firmware;c.sqe_rules.bytes=sizeof firmware;
    NativeAdrenoSha256::hash(firmware,sizeof firmware,c.sqe_rules.sha256);c.total_timeout_us=1000000;
    for(unsigned fault=0;fault<9;++fault){
        K70GpuConfig bad=c;K70Gpu driver;K70GpuResult r;reserve_calls=gmu_calls=0;
        switch(fault){
        case 0:bad.platform.boot_gmu=nullptr;break;
        case 1:bad.a740_profile_verified=0;break;
        case 2:bad.gmu.physical=0x3d6a000;break;
        case 3:bad.slots[1]=0;break;
        case 4:bad.sids[1]=2;break;
        case 5:bad.sqe=nullptr;break;
        case 6:bad.sqe_rules.sha256[0]^=1;break;
        case 7:bad.sqe_bytes=0;break;
        default:bad.total_timeout_us=0;break;
        }
        assert(driver.run(&bad,&r)!=kIOReturnSuccess && !r.hardware_complete && r.resources_must_retain);
        assert(!reserve_calls && !gmu_calls && !fixture.created);
    }
    K70Gpu driver;K70GpuResult r;reserve_calls=gmu_calls=0;
    assert(driver.run(&c,&r)==kIOReturnNotReady && r.stage==K70GpuGmu && !r.hardware_complete);
    assert(reserve_calls==1 && gmu_calls==1 && !fixture.created);
    assert(driver.run(&c,&r)==kIOReturnBusy && !r.hardware_complete);
    revoked=true;K70Gpu lost;reserve_calls=gmu_calls=0;
    assert(lost.run(&c,&r)!=kIOReturnSuccess && reserve_calls==1 && !gmu_calls && !fixture.created);
    revoked=false;gmu_ready=true;
    for(unsigned mode=0;mode<=8;++mode){
        memset(gpu,0,sizeof gpu);memset(gmu,0,sizeof gmu);memset(smmu,0,sizeof smmu);
        auto *sm=reinterpret_cast<uint32_t *>(smmu);
        sm[0x20/4]=(1u<<30)|(1u<<27)|(4u<<9)|8;sm[0x24/4]=0x40000010;sm[0x28/4]=0x1433;sm[0]=0x436;
        for(unsigned i=0;i<8;++i)sm[(0xc00+4*i)/4]=0x20000;
        cp_mode=mode;ticks=0;revoked=false;
        {
            K70Gpu actual;IOReturn status=actual.run(&c,&r);
            if((mode==1 && status)||(mode!=1 && !status))std::fprintf(stderr,"peer mode=%u stage=%u status=%d cp=%u write=%u seen=%u fault=%x\n",mode,unsigned(r.stage),status,r.cp_fence,r.write_fence,r.observed_fence,r.fault_flags);
            /* Uses actual production allocator, SMMU and VM, not their stubs. */
            assert(fixture.live.size()==6);
            for(unsigned i=2;i<5;++i)assert(r.logical_bytes[i]==4096 && r.cpu_owned_bytes[i]==16384 && r.gpu_spans[i]==1);
            if(mode==1)assert(!status && r.hardware_complete && r.stage==K70GpuComplete && r.cp_fence==1 && r.write_fence==2 && r.matched_words==1024);
            else assert(status && !r.hardware_complete && r.resources_must_retain);
            if(mode==0)assert(r.stage==K70GpuCp && r.cp_fence==1 && !r.observed_fence);
            if(mode==2 || mode==5)assert(r.stage==K70GpuScratch);
            if(mode==3)assert(r.stage==K70GpuScratch && r.observed_fence==1);
        }
        /* Test-only arena destruction after the software peer is stopped.
         * Production intentionally has no cleanup after uncertain execution. */
        while(!fixture.live.empty())fixture.live.back()->release();
        while(!fixture.metadata.empty())fixtureMetadataFree(fixture.metadata.back());
    }
    puts("PASS: native K70 core rejects missing GMU/qualification, wrong origin/SID, absent or altered firmware, revoked lease and replay; zero allocations/MMIO after gate failure");
    puts("PASS: real Memory/SMMU/VM/CP/ring path with 16KB ownership and 4KB GPU lengths; software CP peer positive case plus no execution, missing writes/fence, future fence, corruption, revoked lease, SMMU fault and cache-acquire refusals; no GPU hardware tested");
}
