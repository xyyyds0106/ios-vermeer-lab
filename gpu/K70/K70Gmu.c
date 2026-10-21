/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "K70Gmu.h"
#include "AdrenoFirmware.h"
/* Generated only from exact pinned XML/catalog/DT/firmware policy; ignored.
 * In particular, no vendor firmware or sequencer payload is stored in Git. */
#include "K70GmuRegs.h"
static const uint64_t k70_gmu_window_physical[5]={0x3d68000,0x3d50000,0xb280000,0x3d00000,0x3d9e000};
static const size_t k70_gmu_window_bytes[5]={0x37000,0x10000,0x10000,0x40000,0x1000};
#if (!defined(__aarch64__) && !defined(K70_GMU_HOST_TEST)) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error K70 GMU requires little-endian AArch64
#endif
static int extent(uintptr_t at,size_t bytes)
{return at && bytes && bytes-1<=UINTPTR_MAX-at;}
static int overlap(uintptr_t a,size_t na,uintptr_t b,size_t nb)
{return a<=b?(b-a<na):(a-b<nb);}
static size_t core_offset(uint32_t word)
{return ((size_t)word<<2)-0x68000u;}
static int read32(K70GmuDevice *d,unsigned window,size_t at,uint32_t *v,K70GmuResult *r)
{++r->read_attempts;return d->io.read32(d->io.cookie,window,at,v);}
static int write32(K70GmuDevice *d,unsigned window,size_t at,uint32_t v,K70GmuResult *r)
{++r->write_attempts;return d->io.write32(d->io.cookie,window,at,v);}
static int core_read(K70GmuDevice *d,uint32_t word,uint32_t *v,K70GmuResult *r)
{return read32(d,K70GmuCore,core_offset(word),v,r);}
static int core_write(K70GmuDevice *d,uint32_t word,uint32_t v,K70GmuResult *r)
{return write32(d,K70GmuCore,core_offset(word),v,r);}
static K70GmuStatus check_faults(K70GmuDevice *d,K70GmuResult *r)
{
    if(core_read(d,REG_GMU_AO_HOST_INTERRUPT_STATUS,&r->ao_fault_status,r) ||
        core_read(d,REG_GMU_GMU2HOST_INTR_INFO,&r->cm3_fault_status,r))return K70GmuIoError;
    return (r->ao_fault_status&K70_GMU_AO_FATAL_MASK) ||
        (r->cm3_fault_status&K70_GMU_CM3_FATAL_MASK)?K70GmuFault:K70GmuSuccess;
}
static int notify(void *cookie)
{
    K70GmuDevice *d=cookie;
    return d->io.barrier(d->io.cookie) ||
        d->io.write32(d->io.cookie,K70GmuCore,core_offset(REG_GMU_HOST2GMU_INTR_SET),1) ||
        d->io.barrier(d->io.cookie);
}
static K70GmuStatus poll(K70GmuDevice *d,uint32_t reg,uint32_t mask,
    uint32_t expected,unsigned budget,uint32_t *observed,K70GmuResult *r)
{
    uint64_t start,last,now;
    if(d->services.now_us(d->services.cookie,&start))return K70GmuClockError;
    last=start;
    for(unsigned i=0;i<budget;++i) {
        if(d->services.now_us(d->services.cookie,&now) || now<last)return K70GmuClockError;
        if(now-start>=10000)return K70GmuTimeout;
        last=now;++r->poll_reads;
        K70GmuStatus fault=check_faults(d,r);if(fault!=K70GmuSuccess)return fault;
        if(core_read(d,reg,observed,r))return K70GmuIoError;
        if((*observed&mask)==expected) {
            if(d->io.barrier(d->io.cookie))return K70GmuIoError;
            return K70GmuSuccess;
        }
        if(i+1<budget && d->services.wait_us(d->services.cookie,100))return K70GmuClockError;
    }
    return K70GmuTimeout;
}
static void reset_after_failure(K70GmuDevice *d,K70GmuResult *r)
{
    uint32_t actual;r->reset_verified=0;d->poisoned=1;d->hfi.poisoned=1;
    if(!core_write(d,REG_GMU_CM3_SYSRESET,1,r) && !d->io.barrier(d->io.cookie) &&
        !core_read(d,REG_GMU_CM3_SYSRESET,&actual,r) && actual==1)r->reset_verified=1;
}
static uint32_t le32(const uint8_t *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static K70GmuStatus validate(K70GmuDevice *d,const K70GmuConfig *c,K70GmuResult *r)
{
    if(!d || !c || !c->profile || c->profile->qualified!=1 || !c->profile->chip_id ||
        !c->profile->gmu_chip_id || !c->profile->firmware_blocks || c->profile->firmware_blocks>32 ||
        !c->profile->setup || !c->profile->setup_count || c->profile->setup_count>64 || !d->io.read32 || !d->io.write32 || !d->io.barrier ||
        !d->services.sha256 || !d->services.now_us || !d->services.wait_us ||
        c->chip_id!=c->profile->chip_id || !c->firmware || c->firmware_bytes!=c->profile->firmware_bytes ||
        !extent((uintptr_t)c->firmware,c->firmware_bytes) || !c->poll_budget || c->poll_budget>1024)
        return K70GmuArgument;
    if(d->poisoned)return K70GmuPoisoned;
    if(d->boot_attempted)return K70GmuBusy;
    for(unsigned i=0;i<K70GmuWindows;++i)if(d->io.bytes[i]!=k70_gmu_window_bytes[i])return K70GmuArgument;
    const K70GmuMemory *mem[]={&c->icache,&c->dummy,&c->debug,&c->log,&c->hfi};
    const size_t sizes[]={0xffc000,0x4000,0x7000,0x4000,0x4000};
    for(unsigned i=0;i<5;++i) {
        const K70GmuMemory *m=mem[i];
        if(m->bytes!=sizes[i] || !extent((uintptr_t)m->cpu,m->bytes) || ((uintptr_t)m->cpu&4095) ||
            !m->iova || (m->iova&4095) || m->iova>UINT32_MAX-(m->bytes-1) ||
            overlap((uintptr_t)m->cpu,m->bytes,(uintptr_t)c->firmware,c->firmware_bytes))return K70GmuArgument;
        for(unsigned j=0;j<i;++j)if(overlap((uintptr_t)m->cpu,m->bytes,(uintptr_t)mem[j]->cpu,mem[j]->bytes) ||
            overlap((uintptr_t)m->iova,m->bytes,(uintptr_t)mem[j]->iova,mem[j]->bytes))return K70GmuArgument;
    }
    if(c->icache.iova!=0x4000 || c->dummy.iova!=0x60000000 || c->debug.iova!=0x60400000)return K70GmuArgument;
    if(d->services.sha256(d->services.cookie,c->firmware,c->firmware_bytes,r->complete_sha256))return K70GmuFirmware;
    for(unsigned i=0;i<32;++i)if(r->complete_sha256[i]!=c->profile->sha256[i])return K70GmuFirmware;
    for(size_t i=0;i<c->profile->setup_count;++i) {
        const K70GmuSetupWrite *w=c->profile->setup+i;
        if((w->window!=K70GmuRscc && w->window!=K70GmuPdc) || (w->byte_offset&3) ||
            w->byte_offset>d->io.bytes[w->window] || d->io.bytes[w->window]-w->byte_offset<4)return K70GmuArgument;
    }
    r->complete_hash_verified=1;return K70GmuSuccess;
}
K70GmuStatus K70GmuLoadAndStartHfi(K70GmuDevice *d,const K70GmuConfig *c,K70GmuResult *r)
{
    if(!r)return K70GmuArgument;
    *r=(K70GmuResult){0};r->resources_must_retain=1;
    K70GmuStatus status=validate(d,c,r);if(status!=K70GmuSuccess)return status;
    const AdrenoFirmwareRegion regions[]={{0,0x4000,1},{0x4000,0xffc000,2},{0x10004000,0x4000,3}};
    AdrenoFirmwareBlock blocks[32];size_t count=0;
    if(AdrenoFirmwarePlanGmu(c->firmware,c->firmware_bytes,regions,3,blocks,32,&count)!=AdrenoFirmwareSuccess || count!=c->profile->firmware_blocks)
        return K70GmuFirmware;
    r->phase=1;uint32_t value;
    if(core_read(d,REG_GMU_CM3_SYSRESET,&value,r))return K70GmuIoError;
    if(value!=1)return K70GmuBusy;
    r->reset_verified=1;
    if(read32(d,K70GmuCxMisc,REG_CX_MISC_TCM_RET_CNTL*4,&value,r))return K70GmuIoError;
    if(value)return K70GmuStateMismatch;
    if(read32(d,K70GmuRscc,REG_RSCC_SEQ_BUSY_DRV0*4,&value,r))return K70GmuIoError;
    if(value)return K70GmuBusy;
    if(core_read(d,REG_GMU_HFI_CTRL_STATUS,&value,r))return K70GmuIoError;
    if(value&1)return K70GmuStateMismatch; /* A stale ready bit cannot pass. */
    status=check_faults(d,r);if(status!=K70GmuSuccess)return status;
    r->phase=2;
    d->boot_attempted=1;
    for(unsigned i=0;i<c->profile->setup_count;++i) {
        const K70GmuSetupWrite *w=c->profile->setup+i;
        if(write32(d,w->window,w->byte_offset,w->value,r)) {status=K70GmuIoError;goto failed;}
    }
    if(d->io.barrier(d->io.cookie)) {status=K70GmuIoError;goto failed;}
    if(core_write(d,REG_GPU_GMU_CX_GMU_CX_FALNEXT_INTF,1,r) ||
        core_write(d,REG_GPU_GMU_CX_GMU_CX_FAL_INTF,1,r) ||
        write32(d,K70GmuCxMisc,REG_CX_MISC_TCM_RET_CNTL*4,1,r)) {status=K70GmuIoError;goto failed;}
    if(d->io.barrier(d->io.cookie) || read32(d,K70GmuCxMisc,REG_CX_MISC_TCM_RET_CNTL*4,&value,r)) {status=K70GmuIoError;goto failed;}
    if(value!=1) {status=K70GmuStateMismatch;goto failed;}
    /* All shared allocations are real owned coherent memory, not simulated
     * GMU addresses. Zero only after actual stopped/idle checks above. */
    const K70GmuMemory *memory[]={&c->icache,&c->dummy,&c->debug,&c->log};
    r->memory_changed=1;
    for(unsigned m=0;m<4;++m)for(size_t i=0;i<memory[m]->bytes/4;++i)memory[m]->cpu[i]=0;
    if(AdrenoHfiInitializeStopped(&d->hfi,c->hfi.cpu,c->hfi.bytes,c->hfi.iova,d,notify)!=AdrenoHfiSuccess) {
        status=K70GmuArgument;goto failed;
    }
    r->phase=3;
    for(size_t i=0;i<count;++i) {
        const AdrenoFirmwareBlock *b=blocks+i;
        /* Zero-size records authorize no allocation or write. The pinned
         * loader skips them; all real extents were independently supplied. */
        if(!b->bytes)continue;
        for(size_t at=0;at<b->bytes;at+=4) {
            uint32_t word=le32(c->firmware+b->payload_offset+at);
            if(b->region_identifier==2)c->icache.cpu[(b->address-0x4000+at)/4]=word;
            else {
                uint32_t reg=b->region_identifier==1?REG_GMU_CM3_ITCM_START+b->address/4:
                    REG_GMU_CM3_DTCM_START+(b->address-0x10004000)/4;
                if(core_write(d,reg+(uint32_t)(at/4),word,r)) {status=K70GmuIoError;goto failed;}
            }
        }
    }
    if(d->io.barrier(d->io.cookie)) {status=K70GmuIoError;goto failed;}
    /* Read every TCM payload word before releasing reset. CPU cache readback
     * checks copied bytes only; actual GMU coherency is still a prerequisite. */
    for(size_t i=0;i<count;++i) {
        const AdrenoFirmwareBlock *b=blocks+i;
        for(size_t at=0;at<b->bytes;at+=4) {
            if(b->region_identifier==2)value=c->icache.cpu[(b->address-0x4000+at)/4];
            else {
                uint32_t reg=b->region_identifier==1?REG_GMU_CM3_ITCM_START+b->address/4:
                    REG_GMU_CM3_DTCM_START+(b->address-0x10004000)/4;
                if(core_read(d,reg+(uint32_t)(at/4),&value,r)) {status=K70GmuIoError;goto failed;}
            }
            if(value!=le32(c->firmware+b->payload_offset+at)) {status=K70GmuStateMismatch;goto failed;}
        }
    }
    r->phase=4;
    if(core_write(d,REG_GMU_CM3_FW_INIT_RESULT,0,r) ||
        core_write(d,REG_GMU_CM3_BOOT_CONFIG,2,r) ||
        core_write(d,REG_GMU_HFI_QTBL_ADDR,(uint32_t)c->hfi.iova,r) ||
        core_write(d,REG_GMU_HFI_QTBL_INFO,1,r) ||
        core_write(d,REG_GMU_AHB_FENCE_RANGE_0,UINT32_C(0x80000000)|(0x32u<<18)|0x8a0,r) ||
        core_write(d,REG_GMU_CM3_CFG,0x4052,r) ||
        core_write(d,REG_GMU_GENERAL_10,c->profile->gmu_chip_id,r) ||
        core_write(d,REG_GMU_GENERAL_8,(uint32_t)c->log.iova|3,r)) {status=K70GmuIoError;goto failed;}
    for(unsigned i=0;i<4;++i)if(write32(d,K70GmuGpu,(REG_GBIF_QSB_SIDE0+i)*4,0x71620,r)) {status=K70GmuIoError;goto failed;}
    if(core_write(d,REG_GMU_SYS_BUS_CONFIG,1,r) || core_write(d,REG_GMU_ICACHE_CONFIG,1,r) ||
        core_write(d,REG_GMU_DCACHE_CONFIG,1,r) || core_read(d,REG_GMU_CM3_DTCM_START+0xff8,&value,r)) {status=K70GmuIoError;goto failed;}
    uint32_t mask=value<=UINT32_C(0x20010004)?UINT32_MAX:0x1ff;
    uint32_t ready=mask==UINT32_MAX?UINT32_C(0xbabeface):0x100;
    if(core_write(d,REG_GMU_CM3_SYSRESET,1,r) || core_write(d,REG_GMU_GENERAL_9,0,r) ||
        d->io.barrier(d->io.cookie)) {status=K70GmuIoError;goto failed;}
    const uint32_t config_regs[]={REG_GMU_CM3_FW_INIT_RESULT,REG_GMU_CM3_BOOT_CONFIG,
        REG_GMU_HFI_QTBL_ADDR,REG_GMU_HFI_QTBL_INFO,REG_GMU_CM3_CFG,REG_GMU_GENERAL_10,
        REG_GMU_GENERAL_8,REG_GMU_AHB_FENCE_RANGE_0,REG_GMU_SYS_BUS_CONFIG,
        REG_GMU_ICACHE_CONFIG,REG_GMU_DCACHE_CONFIG};
    const uint32_t config_values[]={0,2,(uint32_t)c->hfi.iova,1,0x4052,c->profile->gmu_chip_id,
        (uint32_t)c->log.iova|3,UINT32_C(0x80000000)|(0x32u<<18)|0x8a0,1,1,1};
    for(unsigned i=0;i<sizeof(config_regs)/sizeof(config_regs[0]);++i) {
        if(core_read(d,config_regs[i],&value,r)) {status=K70GmuIoError;goto failed;}
        if(value!=config_values[i]) {status=K70GmuStateMismatch;goto failed;}
    }
    r->reset_verified=0;
    if(core_write(d,REG_GMU_CM3_SYSRESET,0,r) || d->io.barrier(d->io.cookie)) {status=K70GmuIoError;goto failed;}
    r->phase=5;
    status=poll(d,REG_GMU_CM3_FW_INIT_RESULT,mask,ready,c->poll_budget,&r->firmware_init_result,r);
    if(status!=K70GmuSuccess)goto failed;
    r->firmware_ready_observed=1;
    if(core_write(d,REG_GMU_HFI_CTRL_INIT,1,r) || d->io.barrier(d->io.cookie)) {status=K70GmuIoError;goto failed;}
    r->phase=6;
    status=poll(d,REG_GMU_HFI_CTRL_STATUS,1,1,c->poll_budget,&r->hfi_status,r);
    if(status!=K70GmuSuccess)goto failed;
    status=check_faults(d,r);if(status!=K70GmuSuccess)goto failed;
    if(d->io.barrier(d->io.cookie)) {status=K70GmuIoError;goto failed;}
    r->hfi_ready_observed=1;r->phase=7;return K70GmuSuccess;
failed:
    reset_after_failure(d,r);return status;
}
typedef struct {
    K70GmuDevice *device;
    K70GmuStartupResult *result;
} StartupContext;
static int startup_now(void *cookie,uint64_t *value)
{
    K70GmuDevice *d=((StartupContext *)cookie)->device;
    return d->services.now_us(d->services.cookie,value);
}
static int startup_wait(void *cookie,unsigned us)
{
    K70GmuDevice *d=((StartupContext *)cookie)->device;
    return d->services.wait_us(d->services.cookie,us);
}
static int startup_health(void *cookie)
{
    StartupContext *c=cookie;K70GmuDevice *d=c->device;
    K70GmuStartupResult *r=c->result;uint32_t reset,status;
    r->health_status=check_faults(d,&r->load);
    if(r->health_status!=K70GmuSuccess)return 1;
    if(core_read(d,REG_GMU_CM3_SYSRESET,&reset,&r->load) ||
        core_read(d,REG_GMU_HFI_CTRL_STATUS,&status,&r->load))r->health_status=K70GmuIoError;
    else if(reset!=0 || !(status&1))r->health_status=K70GmuStateMismatch;
    return r->health_status!=K70GmuSuccess;
}
K70GmuStatus K70GmuColdStart(K70GmuDevice *d,const K70GmuConfig *c,
    const AdrenoHfiStartupConfig *config,const K70GmuStartupLimits *limits,K70GmuStartupResult *r)
{
    if(!r)return K70GmuArgument;
    *r=(K70GmuStartupResult){0};r->load.resources_must_retain=r->protocol.resources_must_retain=1;
    r->protocol_status=AdrenoHfiStartupInvalid; /* Not attempted until the load succeeds. */
    if(!d || !c || !limits || AdrenoHfiStartupValidate(config)!=AdrenoHfiStartupSuccess)return K70GmuArgument;
    StartupContext context={d,r};
    AdrenoHfiStartupServices services={&context,startup_now,startup_wait,startup_health,
        limits->polls_per_command,limits->wait_interval_us,limits->command_timeout_us,limits->total_timeout_us};
    if(AdrenoHfiStartupValidateServices(&services)!=AdrenoHfiStartupSuccess)return K70GmuArgument;
    if(d->poisoned || d->startup.poisoned)return K70GmuPoisoned;
    if(d->startup.attempted || d->startup.completed)return K70GmuBusy;
    K70GmuStatus status=K70GmuLoadAndStartHfi(d,c,&r->load);
    if(status!=K70GmuSuccess)return status;
    r->protocol_status=AdrenoHfiStartupRun(&d->startup,&d->hfi,config,&services,&r->protocol);
    if(r->protocol_status==AdrenoHfiStartupSuccess)return K70GmuSuccess;
    switch(r->protocol_status) {
    case AdrenoHfiStartupClockError:case AdrenoHfiStartupWaitError:status=K70GmuClockError;break;
    case AdrenoHfiStartupTimeout:status=K70GmuTimeout;break;
    case AdrenoHfiStartupPlatformFault:status=r->health_status;break;
    default:status=K70GmuProtocolError;break;
    }
    reset_after_failure(d,&r->load);d->startup.poisoned=1;
    return status;
}
static int fits(K70GmuMmio *m,unsigned window,size_t at)
{
    if(!m || window>=K70GmuWindows)return 0;
    K70GmuMmioWindow *w=m->windows+window;
    return extent((uintptr_t)w->cpu,w->bytes) && !((uintptr_t)w->cpu&3) && !(at&3) &&
        at<=w->bytes && w->bytes-at>=4;
}
static int mmio_read(void *cookie,unsigned window,size_t at,uint32_t *v)
{K70GmuMmio *m=cookie;if(!v || !fits(m,window,at))return 1;*v=*(volatile uint32_t *)(m->windows[window].cpu+at);return 0;}
static int mmio_write(void *cookie,unsigned window,size_t at,uint32_t v)
{K70GmuMmio *m=cookie;if(!fits(m,window,at))return 1;*(volatile uint32_t *)(m->windows[window].cpu+at)=v;return 0;}
static int mmio_barrier(void *cookie)
{(void)cookie;
#if defined(__aarch64__)
__asm__ volatile("dsb sy":::"memory");
#else
__atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
return 0;}
K70GmuStatus K70GmuBindMmio(K70GmuMmio *m,K70GmuIo *out)
{
    if(!out)return K70GmuArgument;
    *out=(K70GmuIo){0};
    if(!m)return K70GmuArgument;
    for(unsigned i=0;i<K70GmuWindows;++i) {
        const K70GmuMmioWindow *w=m->windows+i;
        if(!fits(m,i,0) || w->physical!=k70_gmu_window_physical[i] ||
            w->bytes!=k70_gmu_window_bytes[i])return K70GmuArgument;
        for(unsigned j=0;j<i;++j)if(overlap((uintptr_t)w->cpu,w->bytes,(uintptr_t)m->windows[j].cpu,m->windows[j].bytes))return K70GmuArgument;
    }
    out->cookie=m;out->read32=mmio_read;out->write32=mmio_write;out->barrier=mmio_barrier;
    for(unsigned i=0;i<K70GmuWindows;++i)out->bytes[i]=m->windows[i].bytes;
    return K70GmuSuccess;
}
