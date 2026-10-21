/* Real GMU validation/native adapters with refused transport. Software only. */
#include "K70GmuNative.h"
#include "K70GmuRegs.h"
#include "K70Zap.h"
#include "K70GpuNative.h"
#include "NativeAdrenoSha256.h"
#include "ArmSmmu4KMmio.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>
extern "C" ArmSmmu4KStatus ArmSmmu4KBindMmio(ArmSmmu4KMmio *,ArmSmmu4KIo *){assert(false);return ArmSmmu4KArgument;}
extern "C" AdrenoHfiStatus AdrenoHfiInitializeStopped(AdrenoHfi *,volatile uint32_t *,size_t,uint64_t,void *,int (*)(void *)){assert(false);return AdrenoHfiInvalid;}
extern "C" AdrenoHfiStatus AdrenoHfiSend(AdrenoHfi *,unsigned,const uint32_t *,size_t,uint32_t *){assert(false);return AdrenoHfiInvalid;}
extern "C" AdrenoHfiStatus AdrenoHfiPoll(AdrenoHfi *,uint32_t *,size_t,size_t *,uint32_t *){assert(false);return AdrenoHfiInvalid;}
extern "C" uintptr_t ml_io_map_unmappable(uintptr_t,uintptr_t,unsigned){assert(false);return 0;}
extern "C" void ml_io_unmap(uintptr_t,uintptr_t){assert(false);}
static unsigned reads,writes,mode,read_fail;
static unsigned power_calls,quarantines;
static unsigned power_mode,configuration_calls;
static AdrenoHfiStartupConfig *power_configuration;
extern "C" int K70GpuPowerAcquire(const K70GpuPowerRequest *,K70GpuPowerLease *){++power_calls;return power_mode?0:1;}
extern "C" int K70GpuPowerValidate(const K70GpuPowerLease *){return power_mode?0:1;}
extern "C" void K70GpuPowerQuarantine(const K70GpuPowerLease *){++quarantines;}
extern "C" int K70GpuPowerConfiguration(const K70GpuPowerLease *,const AdrenoHfiStartupConfig **out,unsigned *perf,unsigned *bw){
    assert(power_mode && power_configuration);++configuration_calls;
    if(power_mode==1)return 1;
    *out=power_mode==2?nullptr:power_configuration;*perf=power_mode==4?99:1;*bw=0;
    if(power_mode==3)power_configuration->gpu[1].hz=219000000;
    if(power_mode==5)power_configuration->gmu[1].hz=219000000;
    return 0;
}
void IODelay(unsigned){assert(false);}
void clock_get_system_microtime(uint64_t *s,uint32_t *us){*s=0;*us=0;}
static int read(void *,unsigned w,size_t at,uint32_t *out){
    ++reads;if(reads==read_fail)return 1;*out=0;
    if(w==0 && at==(REG_GMU_CM3_SYSRESET*4)-0x68000)*out=mode==1?0:1;
    if(w==4 && at==REG_CX_MISC_TCM_RET_CNTL*4)*out=mode==2?1:0;
    if(w==1 && at==REG_RSCC_SEQ_BUSY_DRV0*4)*out=mode==3?1:0;
    if(w==0 && at==(REG_GMU_HFI_CTRL_STATUS*4)-0x68000)*out=mode==4?1:0;
    if(w==0 && at==(REG_GMU_AO_HOST_INTERRUPT_STATUS*4)-0x68000)*out=mode==5?1:0;
    if(w==0 && at==(REG_GMU_GMU2HOST_INTR_INFO*4)-0x68000)*out=mode==6?K70_GMU_CM3_FATAL_MASK:0;
    return 0;
}
static int write(void *,unsigned,size_t,uint32_t){++writes;return 1;}
static int barrier(void *){return 0;}
static int hash(void *,const void *p,size_t n,uint8_t *out){NativeAdrenoSha256::hash(static_cast<const uint8_t *>(p),n,out);return 0;}
static int now(void *,uint64_t *out){*out=0;return 0;}
static int wait(void *,unsigned){return 0;}
static int lease(void *){return 0;}
static void cleanup(){while(!fixture.live.empty())fixture.live.back()->release();while(!fixture.metadata.empty())fixtureMetadataFree(fixture.metadata.back());}
int main(int argc,char **argv){
    if(argc>1){power_mode=(unsigned)atoi(argv[1]);assert(power_mode<=5);}
    const uint8_t abc[]={0x61,0x62,0x63};uint8_t digest[32];NativeAdrenoSha256::hash(abc,3,digest);
    const uint8_t sha_abc[]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    assert(!memcmp(digest,sha_abc,32));
    uint8_t image[20]={0,0,0,0,4,0,0,0};
    K70GmuProfile profile{};profile.qualified=1;profile.chip_id=0x7020100;profile.gmu_chip_id=0x7020100;
    profile.firmware_blocks=1;profile.firmware_bytes=sizeof image;NativeAdrenoSha256::hash(image,sizeof image,profile.sha256);
    K70GmuSetupWrite setup{1,0,0};profile.setup=&setup;profile.setup_count=1;
    K70GmuConfig c{};c.chip_id=profile.chip_id;c.profile=&profile;c.firmware=image;c.firmware_bytes=sizeof image;c.poll_budget=2;
    K70GmuMemory *views[]={&c.icache,&c.dummy,&c.debug,&c.log,&c.hfi};
    const size_t sizes[]={0xffc000,0x4000,0x7000,0x4000,0x4000};const uint64_t iovas[]={0x4000,0x60000000,0x60400000,0x60004000,0x60008000};
    void *allocations[5];for(unsigned i=0;i<5;++i){assert(!posix_memalign(&allocations[i],16384,(sizes[i]+16383)&~size_t(16383)));memset(allocations[i],0xa5,sizes[i]);*views[i]={static_cast<volatile uint32_t *>(allocations[i]),sizes[i],iovas[i]};}
    auto device=[](){K70GmuDevice d{};d.io.read32=read;d.io.write32=write;d.io.barrier=barrier;
        const size_t sizes[]={0x37000,0x10000,0x10000,0x40000,0x1000};for(unsigned i=0;i<5;++i)d.io.bytes[i]=sizes[i];d.services={nullptr,hash,now,wait};return d;};
    for(unsigned fault=0;fault<6;++fault){
        K70GmuConfig bad=c;K70GmuProfile p=profile;bad.profile=&p;K70GmuDevice d=device();K70GmuResult r{};reads=writes=0;
        switch(fault){case 0:p.qualified=0;break;case 1:p.sha256[0]^=1;break;case 2:bad.chip_id++;break;
        case 3:bad.firmware_bytes--;break;case 4:p.firmware_blocks=2;break;default:bad.hfi.bytes=4096;break;}
        assert(K70GmuLoadAndStartHfi(&d,&bad,&r)!=K70GmuSuccess && !reads && !writes && !r.memory_changed && r.resources_must_retain);
    }
    for(mode=1;mode<=6;++mode){K70GmuDevice d=device();K70GmuResult r{};reads=writes=0;
        assert(K70GmuLoadAndStartHfi(&d,&c,&r)!=K70GmuSuccess && reads && !writes && !r.memory_changed && !d.boot_attempted);
    }
    mode=0;for(read_fail=1;read_fail<=6;++read_fail){K70GmuDevice d=device();K70GmuResult r{};reads=writes=0;
        assert(K70GmuLoadAndStartHfi(&d,&c,&r)==K70GmuIoError && !writes && !r.memory_changed);}
    read_fail=0;K70GmuDevice d=device();K70GmuResult report{};reads=writes=0;
    assert(K70GmuLoadAndStartHfi(&d,&c,&report)==K70GmuIoError && d.poisoned && d.boot_attempted && report.resources_must_retain);
    for(unsigned i=0;i<5;++i){assert(static_cast<uint8_t *>(allocations[i])[0]==0xa5);free(allocations[i]);}
    /* Native adapter must refuse before SMMU allocation when the real
     * mapped CM3 SYSRESET register is not actually held at1. */
    alignas(16) static uint8_t core[0x37000],rscc[0x10000],pdc[0x10000],gpu[0x40000],cx[0x1000],smmu[0x40000];
    K70GmuNativeConfig native{};native.lease=lease;native.now_us=now;native.wait_us=wait;
    native.windows[0]={core,0x3d68000,sizeof core};native.windows[1]={rscc,0x3d50000,sizeof rscc};native.windows[2]={pdc,0xb280000,sizeof pdc};native.windows[3]={gpu,0x3d00000,sizeof gpu};native.windows[4]={cx,0x3d9e000,sizeof cx};native.smmu={smmu,0x3da0000,sizeof smmu};
    AdrenoHfiStartupConfig startup{};startup.gpu_count=startup.gmu_count=2;startup.gpu[1].hz=startup.gmu[1].hz=220000000;
    startup.bandwidth_count=startup.cnoc_count=startup.ddr_count=1;startup.cnoc_addresses[0]=0x1000;startup.ddr_addresses[0]=0x2000;
    power_configuration=&startup;
    native.bank=1;native.asid=2;native.slot=2;native.chip_id=profile.chip_id;native.profile=&profile;native.firmware=image;native.firmware_bytes=sizeof image;
    native.startup=&startup;native.perf_index=1;native.total_timeout_us=100000;
    K70GmuNative actual;K70GmuNativeResult result{};
    assert(actual.run(&native,&result)==kIOReturnNotReady && !result.ready && !fixture.created);
    /* Public SoC MDT candidate: exact small ELF with relocatable load/hash.
     * Native host SCM must explicitly REFUSE; no fake authentication path. */
    uint8_t elf[256]={0};memcpy(elf,"\177ELF\1\1\1",7);
    auto put=[&](unsigned at,uint32_t v){memcpy(elf+at,&v,4);};put(28,52);elf[42]=32;elf[44]=3;
    put(52+16,148);put(84,1);put(84+4,160);put(84+12,4096);put(84+16,4);put(84+20,4);put(84+24,1u<<27);
    put(116+4,200);put(116+16,32);put(116+24,2u<<24);
    NativeAdrenoFirmwareRules rules{};rules.bytes=sizeof elf;NativeAdrenoSha256::hash(elf,sizeof elf,rules.sha256);
    K70Zap zap;K70ZapResult zr{};K70ZapServices services{nullptr,lease,now,wait};
    assert(zap.run(elf,sizeof elf,&rules,&services,&zr)==kIOReturnIOError && zr.used && zr.stage==2 && !zr.authenticated && zr.resources_must_retain);
    cleanup();
    const unsigned before_entry_created=fixture.created;
    /* Actual native entry with a refusing power boundary: no MMIO mapping,
     * no fallback boot callback, result/config object retained, no replay. */
    K70GpuNativeInput entry{};entry.gmu_profile=&profile;entry.chip_id=profile.chip_id;
    entry.smmu_actlr_qualified=1;entry.smmu_actlr=0x32b;
    entry.power.gmu_hz=entry.power.gpu_hz=220000000;entry.power.bandwidth_kbps=1;
    entry.sqe=entry.gmu=entry.zap=image;entry.sqe_bytes=entry.gmu_bytes=entry.zap_bytes=sizeof image;
    entry.sqe_rules.bytes=entry.zap_rules.bytes=sizeof image;
    memcpy(entry.sqe_rules.sha256,profile.sha256,32);memcpy(entry.zap_rules.sha256,profile.sha256,32);
    const K70GpuNativeResult *persistent=nullptr;
    K70GpuNativeInput bad=entry;bad.sqe_rules.sha256[0]^=1;
    assert(K70GpuNativeStart(&bad,&persistent)==kIOReturnBadArgument && !persistent && !power_calls);
    for(unsigned fault=0;fault<10;++fault){
        bad=entry;K70GmuProfile p=profile;K70GmuSetupWrite w=setup;
        bad.gmu_profile=&p;p.setup=&w;
        switch(fault){
        case 0:p.qualified=0;break;case 1:p.chip_id++;break;case 2:p.gmu_chip_id=0;break;
        case 3:p.firmware_blocks=0;break;case 4:p.firmware_blocks=33;break;
        case 5:w.window=K70GmuGpu;break;case 6:w.byte_offset=0x10000;break;
        case 7:bad.smmu_actlr_qualified=0;break;case 8:bad.smmu_actlr=0x301;break;
        default:p.gmu_chip_id=0x43050a01;break;
        }
        assert(K70GpuNativeStart(&bad,&persistent)==kIOReturnBadArgument && !persistent && !power_calls);
    }
    assert(K70GpuNativeStart(&entry,&persistent)==kIOReturnNotReady && persistent && persistent->used && persistent->resources_must_retain);
    assert(power_calls==1 && quarantines==1 && !persistent->gmu.ready && !persistent->gpu.hardware_complete);
    assert(configuration_calls==!!power_mode && fixture.created==before_entry_created);
    auto saved=persistent;assert(K70GpuNativeStart(&entry,&persistent)==kIOReturnBusy && !persistent && saved->status==kIOReturnNotReady);
    puts("PASS: production GMU firmware/profile/bounds/refused-reset/RSC/stale-HFI/fatal/read/write failures; native reset gate before allocation; native PAS refuses host SMC; no simulated ACK/authentication");
    puts("PASS: actual retained native entry rejects firmware/profile mismatch before power and refuses power/HFI table failures before mapping; quarantine once and replay blocked");
}
